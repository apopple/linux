// SPDX-License-Identifier: GPL-2.0
#include "../kselftest_harness.h"

#define _GNU_SOURCE
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/capability.h>
#include <unistd.h>

#define CGROUP_TEMP "/sys/fs/cgroup/pins_XXXXXX"
#define PINS_MAX (-1UL)

FIXTURE(pins_cg)
{
	char *cg_path;
	long page_size;
};

static char *cgroup_new(void)
{
	char *cg;

	cg = malloc(sizeof(CGROUP_TEMP));
	strcpy(cg, CGROUP_TEMP);
	if (!mkdtemp(cg)) {
		perror("Failed to create cgroup");
		return NULL;
	}

	return cg;
}

static int cgroup_add_proc(char *cg, pid_t pid)
{
	char *cg_proc;
	FILE *f;
	int ret = 0;

	if (asprintf(&cg_proc, "%s/cgroup.procs", cg) < 0)
		return -1;

	f = fopen(cg_proc, "w");
	free(cg_proc);
	if (!f)
		return -1;

	if (fprintf(f, "%ld\n", (long) pid) < 0)
		ret = -1;

	fclose(f);
	return ret;
}

static int cgroup_set_limit(char *cg, unsigned long limit)
{
	char *cg_pins_max;
	FILE *f;
	int ret = 0;

	if (asprintf(&cg_pins_max, "%s/pins.max", cg) < 0)
		return -1;

	f = fopen(cg_pins_max, "w");
	free(cg_pins_max);
	if (!f)
		return -1;

	if (limit != PINS_MAX) {
		if (fprintf(f, "%ld\n", limit) < 0)
			ret = -1;
	} else {
		if (fprintf(f, "max\n") < 0)
			ret = -1;
	}

	fclose(f);
	return ret;
}

FIXTURE_SETUP(pins_cg)
{
	char *cg_subtree_control;
	FILE *f;

	if (asprintf(&cg_subtree_control,
			"/sys/fs/cgroup/cgroup.subtree_control") < 0)
		return;

	f = fopen(cg_subtree_control, "w");
	free(cg_subtree_control);
	if (!f)
		return;

	fprintf(f, "+pins\n");
	fclose(f);

	self->cg_path = cgroup_new();
	self->page_size = sysconf(_SC_PAGE_SIZE);
}

FIXTURE_TEARDOWN(pins_cg)
{
	char *cg_proc;
	FILE *f;

	if (asprintf(&cg_proc, "%s/cgroup.procs", self->cg_path) < 0)
		return;

	f = fopen(cg_proc, "r");
	free(cg_proc);
	if (!f)
		return;

	while (!feof(f)) {
		long pid;

		if (fscanf(f, "%ld\n", &pid) == EOF)
			break;
		cgroup_add_proc("/sys/fs/cgroup", pid);
	}

	rmdir(self->cg_path);
	free(self->cg_path);
}

static long cgroup_pins(char *cg)
{
	long pin_count;
	char *cg_pins_current;
	FILE *f;
	int ret;

	if (asprintf(&cg_pins_current, "%s/pins.current", cg) < 0)
		return -1;

	f = fopen(cg_pins_current, "r");
	free(cg_pins_current);
	if (!f)
		return -2;

	if (fscanf(f, "%ld", &pin_count) == EOF)
		ret = -3;
	else
		ret = pin_count;

	fclose(f);
	return ret;
}

static int set_rlim_memlock(unsigned long size)
{
	struct rlimit rlim_memlock = {
		.rlim_cur = size,
		.rlim_max = size,
	};
	cap_t cap;
	cap_value_t capability[1] = { CAP_IPC_LOCK };

	/*
	 * Many of the rlimit checks are skipped if a process has
	 * CAP_IP_LOCK. As this test should be run as root we need to
	 * explicitly drop it.
	 */
	cap = cap_get_proc();
	if (!cap)
		return -1;
	if (cap_set_flag(cap, CAP_EFFECTIVE, 1, capability, CAP_CLEAR))
		return -1;
	if (cap_set_proc(cap))
		return -1;
	return setrlimit(RLIMIT_MEMLOCK, &rlim_memlock);
}

TEST_F(pins_cg, basic)
{
	pid_t child_pid;
	char *p;

	ASSERT_EQ(cgroup_add_proc(self->cg_path, getpid()), 0);
	p = mmap(NULL, 32*self->page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	ASSERT_NE(p, MAP_FAILED);
	ASSERT_EQ(cgroup_pins(self->cg_path), 0);
	memset(p, 0, 16*self->page_size);
	ASSERT_EQ(mlock(p, self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 1);
	ASSERT_EQ(mlock(p + self->page_size, self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 2);
	ASSERT_EQ(mlock(p, self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 2);
	ASSERT_EQ(mlock(p, 4*self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 4);
	ASSERT_EQ(cgroup_set_limit(self->cg_path, 8), 0);
	ASSERT_EQ(mlock(p, 16*self->page_size), -1);
	ASSERT_EQ(errno, ENOMEM);
	ASSERT_EQ(cgroup_pins(self->cg_path), 4);
	ASSERT_EQ(cgroup_set_limit(self->cg_path, PINS_MAX), 0);

	/* Exceeds cgroup limit, expected to fail */
	ASSERT_EQ(mlock(p, 16*self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 16);

	/* Exceeds rlimit, expected to fail */
	ASSERT_EQ(set_rlim_memlock(16*self->page_size), 0);
	ASSERT_EQ(mlock(p, 32*self->page_size), -1);
	ASSERT_EQ(errno, ENOMEM);

	child_pid = fork();
	if (!child_pid) {
		memset(p, 0, 16*self->page_size);
		ASSERT_EQ(cgroup_pins(self->cg_path), 16);
		return;

	}
	waitpid(child_pid, NULL, 0);
}

TEST_F(pins_cg, mmap)
{
	char *p;

	ASSERT_EQ(cgroup_add_proc(self->cg_path, getpid()), 0);
	p = mmap(NULL, 4*self->page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE | MAP_LOCKED, -1, 0);
	ASSERT_NE(p, MAP_FAILED);
	ASSERT_EQ(cgroup_pins(self->cg_path), 4);
}

/*
 * Test moving to a different cgroup.
 */
TEST_F(pins_cg, move_cg)
{
	char *p, *new_cg;

	ASSERT_EQ(cgroup_add_proc(self->cg_path, getpid()), 0);
	p = mmap(NULL, 16*self->page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	ASSERT_NE(p, MAP_FAILED);
	memset(p, 0, 16*self->page_size);
	ASSERT_EQ(mlock(p, 16*self->page_size), 0);
	ASSERT_EQ(cgroup_pins(self->cg_path), 16);
	ASSERT_NE(new_cg = cgroup_new(), NULL);
	ASSERT_EQ(cgroup_add_proc(new_cg, getpid()), 0);
	ASSERT_EQ(cgroup_pins(new_cg), 16);
	ASSERT_EQ(cgroup_add_proc(self->cg_path, getpid()), 0);
	rmdir(new_cg);
}
TEST_HARNESS_MAIN
