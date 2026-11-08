/* SPDX-License-Identifier: GPL-2.0-only */
#include "trial-policy.h"
#include <linux/mtd/partitions.h>
static unsigned long confirm_start;
static unsigned int confirm_blocks;
static bool write_enable;
module_param(confirm_start, ulong, 0400);
module_param(confirm_blocks, uint, 0400);
module_param(write_enable, bool, 0400);

static bool slc_armed(void)
{
	return write_enable && confirm_start == SL_TRIAL_START && confirm_blocks == SL_TRIAL_BLOCKS;
}
static void slc_stop(struct marvell_nfc *nfc)
{
	writel(NDCR_ALL_INT, nfc->regs + NDCR);
	writel(0xffffffff, nfc->regs + NDSR);
}
static void slc_start(struct marvell_nfc *nfc, bool ecc)
{
	writel(0, nfc->regs + NDCR);
	readl(nfc->regs + NDECCCTRL);
	writel(ecc ? 48U << 24 : 0, nfc->regs + NDECCCTRL);
	readl(nfc->regs + NDECCCTRL);
	if (ecc)
		writel((48U << 24) | NDECCCTRL_BCH_EN, nfc->regs + NDECCCTRL);
	readl(nfc->regs + NDECCCTRL);
	writel(0xffffffff, nfc->regs + NDSR);
	writel(0x0106f000 | NDCR_ALL_INT | NDCR_ND_RUN | (ecc ? NDCR_ECC_EN : 0), nfc->regs + NDCR);
}
static int slc_idle(struct marvell_nfc *nfc)
{
	u32 control;
	return readl_relaxed_poll_timeout_atomic(nfc->regs + NDCR, control,
						!(control & NDCR_ND_RUN), 1, POLL_TIMEOUT);
}
static int slc_read_status(struct marvell_nfc *nfc, u8 *status)
{
	u32 words[2];
	int ret;
	slc_start(nfc, false);
	ret = berlin_send_read_command(nfc, (4U << 21) | BIT(16) | 0x70, 0, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDDREQ);
	if (!ret) {
		writel(NDSR_RDDREQ, nfc->regs + NDSR);
		ioread32_rep(nfc->regs + NDDB, words, 2);
		*status = words[0] & 0xff;
		ret = berlin_wait_status(nfc, NDSR_CMDD(0));
	}
	slc_stop(nfc);
	return ret;
}
/* -EIO only for the chip's own FAIL bit, a media result the caller may
 * contain. Not-ready or write-protected status means the chip/controller
 * state is not what was commanded. */
static int slc_status(struct marvell_nfc *nfc)
{
	u8 status;
	int ret = slc_read_status(nfc, &status);
	if (ret)
		return ret;
	if ((status & 0xc0) != 0xc0) {
		pr_err("steamlink slc: unexpected NAND status %02x\n", status);
		return -EREMOTEIO;
	}
	if (status & 0x01) {
		pr_err("steamlink slc: NAND status FAIL %02x\n", status);
		return -EIO;
	}
	return 0;
}
static int slc_erase_page(struct nand_chip *chip, unsigned int row)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u64 offset = (u64)row << 12;
	int ret;
	if (!slc_armed() || !sl_trial_erase_allowed(offset, 1048576))
		return -EROFS;
	if (steamlink_readonly_block_isbad(nand_to_mtd(chip), offset))
		return -EIO;
	marvell_nfc_select_target(chip, chip->cur_cs);
	slc_start(nfc, false);
	ret = berlin_send_read_command(nfc, (2U << 21) | BIT(25) | (3U << 16) | BIT(19) | 0xd060,
				      row, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDY(0));
	if (!ret)
		ret = slc_idle(nfc);
	slc_stop(nfc);
	if (!ret)
		ret = slc_status(nfc);
	return ret;
}
static int slc_write_page(struct nand_chip *chip, const u8 *data, int oob_required, int page)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u64 offset = (u64)page << 12;
	unsigned int chunk;
	int ret = 0;
	if (!slc_armed() || !sl_trial_program_allowed(offset, 4096))
		return -EROFS;
	if (steamlink_readonly_block_isbad(nand_to_mtd(chip), offset))
		return -EIO;
	if (!oob_required)
		memset(chip->oob_poi, 0xff, nand_to_mtd(chip)->oobsize);
	marvell_nfc_select_target(chip, chip->cur_cs);
	slc_start(nfc, true);
	for (chunk = 0; chunk < 2; chunk++) {
		u32 command = ((chunk ? 5U : 4U) << 29) | BIT(28) | (1U << 21) | BIT(20) | 0x80;
		if (!chunk)
			command |= 5U << 16;
		ret = berlin_send_read_command(nfc, command,
			chunk ? 0 : (page & 0xffff) << 16, chunk ? 0 : (page >> 16) & 0xff,
			2048 + (chunk ? 32 : 0));
		if (ret)
			goto out;
		ret = berlin_wait_status(nfc, NDSR_WRDREQ);
		if (ret)
			goto out;
		writel(NDSR_WRDREQ, nfc->regs + NDSR);
		iowrite32_rep(nfc->regs + NDDB, data + chunk * 2048, 512);
		if (chunk)
			iowrite32_rep(nfc->regs + NDDB, chip->oob_poi, 8);
	}
	ret = berlin_send_read_command(nfc, (3U << 29) | (1U << 21) | BIT(26) | BIT(19) | 0x10ff, 0, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDY(0));
	if (!ret)
		ret = slc_idle(nfc);
out:
	slc_stop(nfc);
	if (!ret)
		ret = slc_status(nfc);
	return ret;
}

static bool slc_is_erase_op(const struct nand_operation *op)
{
	return op->cs == 0 && op->ninstrs == 4 &&
	       op->instrs[0].type == NAND_OP_CMD_INSTR && op->instrs[0].ctx.cmd.opcode == 0x60 &&
	       op->instrs[1].type == NAND_OP_ADDR_INSTR && op->instrs[1].ctx.addr.naddrs == 3 &&
	       op->instrs[2].type == NAND_OP_CMD_INSTR && op->instrs[2].ctx.cmd.opcode == 0xd0 &&
	       op->instrs[3].type == NAND_OP_WAITRDY_INSTR;
}
static bool slc_is_status_op(const struct nand_operation *op)
{
	return op->cs == 0 && op->ninstrs == 2 &&
	       op->instrs[0].type == NAND_OP_CMD_INSTR && op->instrs[0].ctx.cmd.opcode == 0x70 &&
	       op->instrs[1].type == NAND_OP_DATA_IN_INSTR && op->instrs[1].ctx.data.len == 1;
}
static unsigned int slc_erase_row(const struct nand_operation *op)
{
	const u8 *a = op->instrs[1].ctx.addr.addrs;
	return a[0] | (unsigned int)a[1] << 8 | (unsigned int)a[2] << 16;
}

static int slc_pair_get_info(struct mtd_info *mtd, int page, struct mtd_pairing_info *info)
{
	int rank = sl_test_candidate_lower_index(page);
	if (rank < 0)
		return -EOPNOTSUPP; /* Upper-page pairing has not been established. */
	info->group = 0;
	info->pair = rank;
	return 0;
}
static int slc_pair_get_page(struct mtd_info *mtd, const struct mtd_pairing_info *info)
{
	if (info->group != 0)
		return -EOPNOTSUPP;
	return sl_test_candidate_lower_page(info->pair);
}
static const struct mtd_pairing_scheme slc_candidate_pairing = {
	.ngroups = 2, .get_info = slc_pair_get_info, .get_wunit = slc_pair_get_page,
};
static const struct mtd_partition slc_trial_parts[] = {
	{ .name = "steamlink-slc-trial", .offset = SL_TRIAL_START, .size = 32ULL << 20,
	  .add_flags = MTD_SLC_ON_MLC_EMULATION },
	{ .name = "steamlink-trial-physical-ro", .offset = SL_TRIAL_START, .size = 32ULL << 20,
	  .mask_flags = MTD_WRITEABLE },
	{ .name = "steamlink-trial-left-ro", .offset = SL_TRIAL_START - (1 << 20), .size = 1 << 20,
	  .mask_flags = MTD_WRITEABLE },
	{ .name = "steamlink-trial-right-ro", .offset = SL_TRIAL_END, .size = 1 << 20,
	  .mask_flags = MTD_WRITEABLE },
};
static int slc_block_isbad(struct mtd_info *mtd, loff_t offset)
{
	return steamlink_readonly_block_isbad(mtd, offset);
}
static int slc_refuse_markbad(struct mtd_info *mtd, loff_t offset)
{
	/* Trial fails closed on media retirement; no unqualified BBT writeback. */
	return -EROFS;
}
static int slc_known_bad(struct nand_chip *chip, loff_t offset)
{
	int bad = steamlink_readonly_block_isbad(nand_to_mtd(chip), offset);
	if (bad)
		return bad;
	return steamlink_readonly_block_isreserved(nand_to_mtd(chip), offset);
}
