// SPDX-License-Identifier: GPL-2.0
#include <asm/machdep.h>

static int __init baremetal_probe(void)
{
	return 1;
}

define_machine(baremetal) {
	.name			= "Baremetal PPC",
	.probe			= baremetal_probe,
	.setup_arch		= NULL,
	.init_IRQ		= NULL,
	.show_cpuinfo		= NULL,
	.get_proc_freq          = NULL,
	.progress		= NULL,
	.machine_shutdown	= NULL,
	.power_save             = NULL,
	.calibrate_decr		= generic_calibrate_decr,
	.machine_check_early	= NULL,
};
