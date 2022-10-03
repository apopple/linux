// SPDX-License-Identifier: GPL-2.0-only
/*
 * Controller for cgroups limiting number of pages pinned for FOLL_LONGETERM.
 *
 * Copyright (C) 2022 Alistair Popple <apopple@nvidia.com>
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/sched/task.h>

#define PINS_MAX (-1ULL)
#define PINS_MAX_STR "max"

struct pins_cgroup {
	struct cgroup_subsys_state	css;

	atomic64_t			counter;
	atomic64_t			limit;

	struct cgroup_file		events_file;
	atomic64_t			events_limit;
};

static struct pins_cgroup *css_pins(struct cgroup_subsys_state *css)
{
	return container_of(css, struct pins_cgroup, css);
}

static struct pins_cgroup *parent_pins(struct pins_cgroup *pins)
{
	return css_pins(pins->css.parent);
}

struct pins_cgroup *get_pins_cg(struct task_struct *task)
{
	return css_pins(task_get_css(task, pins_cgrp_id));
}
EXPORT_SYMBOL(get_pins_cg);

void put_pins_cg(struct pins_cgroup *cg)
{
	css_put(&cg->css);
}
EXPORT_SYMBOL(put_pins_cg);

static struct cgroup_subsys_state *
pins_css_alloc(struct cgroup_subsys_state *parent)
{
	struct pins_cgroup *pins;

	pins = kzalloc(sizeof(struct pins_cgroup), GFP_KERNEL);
	if (!pins)
		return ERR_PTR(-ENOMEM);

	atomic64_set(&pins->counter, 0);
	atomic64_set(&pins->limit, PINS_MAX);
	atomic64_set(&pins->events_limit, 0);
	return &pins->css;
}

static void pins_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_pins(css));
}

/**
 * pins_cancel - uncharge the local pin count
 * @pins: the pin cgroup state
 * @num: the number of pins to cancel
 *
 * This function will WARN if the pin count goes under 0, because such a case is
 * a bug in the pins controller proper.
 */
void pins_cancel(struct pins_cgroup *pins, int num)
{
	/*
	 * A negative count (or overflow for that matter) is invalid,
	 * and indicates a bug in the `pins` controller proper.
	 */
	WARN_ON_ONCE(atomic64_add_negative(-num, &pins->counter));
}

/**
 * pins_uncharge - hierarchically uncharge the pin count
 * @pins: the pin cgroup state
 * @num: the number of pins to uncharge
 */
void pins_uncharge(struct pins_cgroup *pins, int num)
{
	struct pins_cgroup *p;

	for (p = pins; parent_pins(p); p = parent_pins(p))
		pins_cancel(p, num);
}
EXPORT_SYMBOL(pins_uncharge);

/**
 * pins_charge - hierarchically charge the pin count
 * @pins: the pin cgroup state
 * @num: the number of pins to charge
 *
 * This function does *not* follow the pin limit set. It cannot fail and the new
 * pin count may exceed the limit. This is only used for reverting failed
 * attaches, where there is no other way out than violating the limit.
 */
static void pins_charge(struct pins_cgroup *pins, int num)
{
	struct pins_cgroup *p;

	for (p = pins; parent_pins(p); p = parent_pins(p))
		atomic64_add(num, &p->counter);
}

/**
 * pins_try_charge - hierarchically try to charge the pin count
 * @pins: the pin cgroup state
 * @num: the number of pins to charge
 *
 * This function follows the set limit. It will fail if the charge would cause
 * the new value to exceed the hierarchical limit. Returns 0 if the charge
 * succeeded, otherwise -EAGAIN.
 */
int pins_try_charge(struct pins_cgroup *pins, int num)
{
	struct pins_cgroup *p, *q;

	for (p = pins; parent_pins(p); p = parent_pins(p)) {
		uint64_t new = atomic64_add_return(num, &p->counter);
		uint64_t limit = atomic64_read(&p->limit);

		if (limit != PINS_MAX && new > limit)
			goto revert;
	}

	return 0;

revert:
	for (q = pins; q != p; q = parent_pins(q))
		pins_cancel(q, num);
	pins_cancel(p, num);

	return -EAGAIN;
}
EXPORT_SYMBOL(pins_try_charge);

static int pins_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *dst_css;
	struct task_struct *task;

	cgroup_taskset_for_each(task, dst_css, tset) {
		struct pins_cgroup *pins = css_pins(dst_css);
		struct cgroup_subsys_state *old_css;
		struct pins_cgroup *old_pins;

		old_css = task_css(task, pins_cgrp_id);
		old_pins = css_pins(old_css);

		pins_charge(pins, task->mm->locked_vm);
		pins_uncharge(old_pins, task->mm->locked_vm);
	}

	return 0;
}

static void pins_cancel_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *dst_css;
	struct task_struct *task;

	cgroup_taskset_for_each(task, dst_css, tset) {
		struct pins_cgroup *pins = css_pins(dst_css);
		struct cgroup_subsys_state *old_css;
		struct pins_cgroup *old_pins;

		old_css = task_css(task, pins_cgrp_id);
		old_pins = css_pins(old_css);

		pins_charge(old_pins, task->mm->locked_vm);
		pins_uncharge(pins, task->mm->locked_vm);
	}
}


static ssize_t pins_max_write(struct kernfs_open_file *of, char *buf,
			      size_t nbytes, loff_t off)
{
	struct cgroup_subsys_state *css = of_css(of);
	struct pins_cgroup *pins = css_pins(css);
	uint64_t limit;
	int err;

	buf = strstrip(buf);
	if (!strcmp(buf, PINS_MAX_STR)) {
		limit = PINS_MAX;
		goto set_limit;
	}

	err = kstrtoll(buf, 0, &limit);
	if (err)
		return err;

	if (limit < 0 || limit >= PINS_MAX)
		return -EINVAL;

set_limit:
	/*
	 * Limit updates don't need to be mutex'd, since it isn't
	 * critical that any racing fork()s follow the new limit.
	 */
	atomic64_set(&pins->limit, limit);
	return nbytes;
}

static int pins_max_show(struct seq_file *sf, void *v)
{
	struct cgroup_subsys_state *css = seq_css(sf);
	struct pins_cgroup *pins = css_pins(css);
	uint64_t limit = atomic64_read(&pins->limit);

	if (limit >= PINS_MAX)
		seq_printf(sf, "%s\n", PINS_MAX_STR);
	else
		seq_printf(sf, "%lld\n", limit);

	return 0;
}

static s64 pins_current_read(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	struct pins_cgroup *pins = css_pins(css);

	return atomic64_read(&pins->counter);
}

static int pins_events_show(struct seq_file *sf, void *v)
{
	struct pins_cgroup *pins = css_pins(seq_css(sf));

	seq_printf(sf, "max %lld\n", (s64)atomic64_read(&pins->events_limit));
	return 0;
}

static struct cftype pins_files[] = {
	{
		.name = "max",
		.write = pins_max_write,
		.seq_show = pins_max_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "current",
		.read_s64 = pins_current_read,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "events",
		.seq_show = pins_events_show,
		.file_offset = offsetof(struct pins_cgroup, events_file),
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{ }	/* terminate */
};

struct cgroup_subsys pins_cgrp_subsys = {
	.css_alloc = pins_css_alloc,
	.css_free = pins_css_free,
	.legacy_cftypes = pins_files,
	.dfl_cftypes = pins_files,
	.can_attach = pins_can_attach,
	.cancel_attach = pins_cancel_attach,
};
