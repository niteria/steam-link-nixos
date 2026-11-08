// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include "nand-overlay-data.h"

static int overlay_id = -1;

static int __init overlay_init(void)
{
	if (!of_machine_is_compatible("valve,steamlink"))
		return -ENODEV;
	return of_overlay_fdt_apply(nand_dtbo, nand_dtbo_len, &overlay_id, NULL);
}

static void __exit overlay_exit(void)
{
	int ret = of_overlay_remove(&overlay_id);
	if (ret)
		pr_err("steamlink-nand-overlay: removal failed: %d\n", ret);
}

module_init(overlay_init);
module_exit(overlay_exit);
MODULE_SOFTDEP("pre: steamlink_marvell_nand");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Steam Link NAND runtime device-tree overlay");
