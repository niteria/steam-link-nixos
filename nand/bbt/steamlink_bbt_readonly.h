/* SPDX-License-Identifier: GPL-2.0-only */
#include "policy.h"
/* The driver refuses other boards/chips/geometries and has one target. */
static unsigned char steamlink_bbt_map[SL_BBT_MAP_BYTES];
static bool steamlink_bbt_ready;

static int steamlink_import_stock_bbt(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u8 *pages;
	unsigned int block, bad = 0;
	u32 failed = mtd->ecc_stats.failed;
	int ret, version;
	steamlink_bbt_ready = false;
	pages = kmalloc(8192, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;
	ret = berlin_read_page(chip, pages, 4095 * 256);
	if (ret < 0)
		goto out;
	if (mtd->ecc_stats.failed != failed) {
		ret = -EBADMSG;
		goto out;
	}
	ret = -EINVAL;
	if (memcmp(chip->oob_poi + 8, "Bbt0", 4))
		goto out;
	version = chip->oob_poi[12];
	ret = berlin_read_page(chip, pages + 4096, 4094 * 256);
	if (ret < 0)
		goto out;
	if (mtd->ecc_stats.failed != failed) {
		ret = -EBADMSG;
		goto out;
	}
	ret = -EINVAL;
	if (memcmp(chip->oob_poi + 8, "1tbB", 4) || chip->oob_poi[12] != version ||
	    memcmp(pages, pages + 4096, 4096))
		goto out;
	memcpy(steamlink_bbt_map, pages, SL_BBT_MAP_BYTES);
	for (block = 0; block < SL_BBT_BLOCKS; block++)
		bad += sl_bbt_bad(steamlink_bbt_map, (u64)block << 20);
	mtd->ecc_stats.badblocks = bad;
	mtd->ecc_stats.bbtblocks = SL_BBT_BLOCKS - SL_BBT_RESERVED_START;
	steamlink_bbt_ready = true;
	dev_info(nfc->dev, "Read-only stock BBT v%d: %u bad, 8 reserved blocks\n", version, bad);
	ret = 0;
out:
	kfree(pages);
	if (ret)
		dev_err(nfc->dev, "Stock BBT copies invalid/inconsistent; refusing MTD registration\n");
	return ret;
}

static int steamlink_readonly_block_isbad(struct mtd_info *mtd, loff_t offset)
{
	int ret;
	if (!steamlink_bbt_ready)
		return -EAGAIN;
	ret = sl_bbt_bad(steamlink_bbt_map, offset);
	return ret < 0 ? -EINVAL : ret;
}

static int steamlink_readonly_block_isreserved(struct mtd_info *mtd, loff_t offset)
{
	int ret = sl_bbt_reserved(offset);
	return ret < 0 ? -EINVAL : ret;
}
