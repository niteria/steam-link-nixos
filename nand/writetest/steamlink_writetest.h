/* SPDX-License-Identifier: GPL-2.0-only */
/* Private experiment, included only by the separate test-module derivation.
 * No writable MTD interface: test runs before read-only MTD registration. */
#include "guard.h"
#include "pattern.h"
#include <linux/timekeeping.h>
#include <linux/math64.h>

static unsigned int confirm_block;
static unsigned int program_pages = 1;
static bool inspect_only = true;
static bool erase_only;
static bool hold_for_stock;
static bool lower_pages_only;
static char test_result[512] = "not-run";
static unsigned int test_program_us[256];
static unsigned int test_physical_pages[256];
module_param(confirm_block, uint, 0400);
module_param(program_pages, uint, 0400);
module_param(inspect_only, bool, 0400);
module_param(erase_only, bool, 0400);
module_param(hold_for_stock, bool, 0400);
module_param(lower_pages_only, bool, 0400);
module_param_string(test_result, test_result, sizeof(test_result), 0400);
module_param_array_named(program_us, test_program_us, uint, NULL, 0400);
module_param_array_named(physical_pages, test_physical_pages, uint, NULL, 0400);
static bool test_has_run, test_armed;
static unsigned int test_program_count;
static u8 test_pattern[4096] __aligned(4);
static u8 test_spare[32] __aligned(4);
static u8 test_bbt[1024];
static int initial_status = -1, erase_status = -1, program_status = -1, cleanup_status = -1;

static void sl_test_stop(struct marvell_nfc *nfc)
{
	writel(NDCR_ALL_INT, nfc->regs + NDCR);
	writel(0xffffffff, nfc->regs + NDSR);
}

static void sl_test_start(struct marvell_nfc *nfc, bool ecc)
{
	writel(0, nfc->regs + NDCR);
	readl(nfc->regs + NDECCCTRL);
	writel(ecc ? 48U << 24 : 0, nfc->regs + NDECCCTRL);
	readl(nfc->regs + NDECCCTRL);
	if (ecc)
		writel((48U << 24) | NDECCCTRL_BCH_EN, nfc->regs + NDECCCTRL);
	readl(nfc->regs + NDECCCTRL);
	writel(0xffffffff, nfc->regs + NDSR);
	writel(0x0106f000 | NDCR_ALL_INT | NDCR_ND_RUN |
	       (ecc ? NDCR_ECC_EN : 0), nfc->regs + NDCR);
}

static int sl_test_wait_idle(struct marvell_nfc *nfc)
{
	u32 control;
	return readl_relaxed_poll_timeout_atomic(nfc->regs + NDCR, control,
						!(control & NDCR_ND_RUN), 1, POLL_TIMEOUT);
}

static int sl_test_status(struct marvell_nfc *nfc, int *status)
{
	u32 words[2];
	int ret;
	sl_test_start(nfc, false);
	/* Stock PIO STATUS: command type 4, one address cycle, eight FIFO bytes. */
	ret = berlin_send_read_command(nfc, (4U << 21) | BIT(16) | 0x70, 0, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDDREQ);
	if (!ret) {
		writel(NDSR_RDDREQ, nfc->regs + NDSR);
		ioread32_rep(nfc->regs + NDDB, words, 2);
		*status = words[0] & 0xff;
		ret = berlin_wait_status(nfc, NDSR_CMDD(0));
	}
	sl_test_stop(nfc);
	return ret;
}

static int sl_test_reset(struct marvell_nfc *nfc)
{
	int ret;
	sl_test_start(nfc, false);
	ret = berlin_send_read_command(nfc, (5U << 21) | 0xff, 0, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDY(0));
	sl_test_stop(nfc);
	return ret;
}

static int sl_test_erase(struct nand_chip *chip, u64 offset, u64 length, int *status)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	int ret;
	/* Check the full-width byte address before any address encoding or MMIO. */
	if (!sl_test_erase_allowed(offset, length) || !test_armed)
		return -EPERM;
	sl_test_start(nfc, false);
	/* Stock PIO ERASE: three row cycles, automatic status, 60h/D0h pair. */
	ret = berlin_send_read_command(nfc, (2U << 21) | BIT(25) | (3U << 16) |
				      BIT(19) | 0xd060, offset / 4096, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDY(0));
	if (!ret)
		ret = sl_test_wait_idle(nfc);
	sl_test_stop(nfc);
	if (!ret)
		ret = sl_test_status(nfc, status);
	if (!ret && ((*status & 0xc1) != 0xc0))
		ret = -EIO;
	return ret;
}

static int sl_test_program(struct nand_chip *chip, u64 offset, u64 length)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 row, command;
	unsigned int chunk;
	int ret = 0;
	unsigned int next = lower_pages_only ? sl_test_candidate_lower_page(test_program_count) : test_program_count;
	if (!sl_test_program_next(offset, length, next) || !test_armed ||
	    test_program_count >= program_pages)
		return -EPERM;
	test_program_count++;
	row = offset / 4096;
	sl_test_start(nfc, true);
	/* Exact stock PIO dispatch: 80h+address+first chunk, naked second chunk,
	 * then final 10h. No cached/multiplane programming or feature writes. */
	for (chunk = 0; chunk < 2; chunk++) {
		command = ((chunk ? 5U : 4U) << 29) | BIT(28) | (1U << 21) |
			  BIT(20) | 0x80;
		if (!chunk)
			command |= 5U << 16;
		ret = berlin_send_read_command(nfc, command,
			chunk ? 0 : (row & 0xffff) << 16, chunk ? 0 : row >> 16,
			2048 + (chunk ? 32 : 0));
		if (ret)
			goto out;
		ret = berlin_wait_status(nfc, NDSR_WRDREQ);
		if (ret)
			goto out;
		writel(NDSR_WRDREQ, nfc->regs + NDSR);
		iowrite32_rep(nfc->regs + NDDB, test_pattern + chunk * 2048, 512);
		if (chunk)
			iowrite32_rep(nfc->regs + NDDB, test_spare, 8);
	}
	ret = berlin_send_read_command(nfc, (3U << 29) | (1U << 21) | BIT(26) |
				      BIT(19) | 0x10ff, 0, 0, 0);
	if (!ret)
		ret = berlin_wait_status(nfc, NDSR_RDY(0));
	if (!ret)
		ret = sl_test_wait_idle(nfc);
out:
	sl_test_stop(nfc);
	if (!ret)
		ret = sl_test_status(nfc, &program_status);
	if (!ret && ((program_status & 0xc1) != 0xc0))
		ret = -EIO;
	return ret;
}

static int sl_test_check_block(struct nand_chip *chip, u8 *buffer,
			       unsigned int block, bool programmed)
{
	unsigned int i;
	u32 failed = nand_to_mtd(chip)->ecc_stats.failed;
	int ret;
	for (i = 0; i < 256; i++) {
		ret = berlin_read_page(chip, buffer, block * 256 + i);
		if (ret < 0)
			return ret;
		if (nand_to_mtd(chip)->ecc_stats.failed != failed)
			return -EBADMSG;
		if (programmed && (lower_pages_only ? sl_test_candidate_lower_index(i) >= 0 : i < program_pages)) {
			sl_test_pattern(i, test_pattern, test_spare);
			if (memcmp(buffer, test_pattern, 4096) ||
			    memcmp(chip->oob_poi, test_spare, 32))
				return -EILSEQ;
		} else if (memchr_inv(buffer, 0xff, 4096) ||
			   memchr_inv(chip->oob_poi, 0xff, 32)) {
			return -ENOTEMPTY;
		}
	}
	return 0;
}

static int sl_test_check_bbt(struct nand_chip *chip, u8 *buffer)
{
	int ret, version;
	u32 failed = nand_to_mtd(chip)->ecc_stats.failed;
	unsigned int block;
	ret = berlin_read_page(chip, buffer, 4095 * 256);
	if (ret < 0 || nand_to_mtd(chip)->ecc_stats.failed != failed || memcmp(chip->oob_poi + 8, "Bbt0", 4))
		return -EINVAL;
	version = chip->oob_poi[12];
	memcpy(test_bbt, buffer, sizeof(test_bbt));
	ret = berlin_read_page(chip, buffer, 4094 * 256);
	if (ret < 0 || nand_to_mtd(chip)->ecc_stats.failed != failed || memcmp(chip->oob_poi + 8, "1tbB", 4) ||
	    chip->oob_poi[12] != version || memcmp(test_bbt, buffer, sizeof(test_bbt)))
		return -EINVAL;
	for (block = SL_TEST_BLOCK - 1; block <= SL_TEST_BLOCK + 1; block++)
		if (((test_bbt[block / 4] >> ((block % 4) * 2)) & 3) != 3)
			return -EIO;
	return 0;
}

static void steamlink_run_writetest(struct nand_chip *chip)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u8 *buffer;
	int ret = -EPERM, cleanup = 0, neighbors = 0;
	bool touched = false, verified = false, empty = false;
	const char *phase = "authorization";
	unsigned int i;
	if (confirm_block != SL_TEST_BLOCK || test_has_run || !program_pages || program_pages > 256)
		goto report;
	if (hold_for_stock && !sl_test_hold_allowed(inspect_only, erase_only, program_pages))
		goto report;
	if (lower_pages_only && (program_pages != 128 || hold_for_stock))
		goto report;
	test_has_run = true;
	buffer = kmalloc(4096, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto report;
	}
	phase = "bbt";
	ret = sl_test_check_bbt(chip, buffer);
	if (ret)
		goto free;
	phase = "neighbors-before";
	ret = sl_test_check_block(chip, buffer, SL_TEST_BLOCK - 1, false);
	if (!ret)
		ret = sl_test_check_block(chip, buffer, SL_TEST_BLOCK + 1, false);
	if (ret)
		goto free;
	phase = "target-before";
	ret = sl_test_check_block(chip, buffer, SL_TEST_BLOCK, false);
	if (ret && !(erase_only && !inspect_only && ret == -ENOTEMPTY))
		goto free;
	phase = "status-before";
	ret = sl_test_status(nfc, &initial_status);
	if (ret || (initial_status & 0xc0) != 0xc0) {
		if (!ret)
			ret = -EACCES;
		goto free;
	}
	if (inspect_only) {
		phase = "inspection-passed";
		goto free;
	}
	test_armed = true;
	phase = "initial-erase";
	touched = true;
	ret = sl_test_erase(chip, SL_TEST_OFFSET, SL_TEST_ERASESIZE, &erase_status);
	if (!ret)
		ret = berlin_read_page(chip, buffer, 0); /* qualified BCH warm-up */
	if (ret >= 0)
		ret = sl_test_check_block(chip, buffer, SL_TEST_BLOCK, false);
	if (ret)
		goto cleanup;
	if (!erase_only) {
		phase = "program";
		/* Micron l73a Rev D p88: strictly ascending pages, NOP=1. */
		for (i = 0; i < program_pages; i++) {
			u64 started;
			unsigned int physical_page = lower_pages_only ? sl_test_candidate_lower_page(i) : i;
			sl_test_pattern(physical_page, test_pattern, test_spare);
			test_physical_pages[i] = physical_page;
			started = ktime_get_ns();
			ret = sl_test_program(chip, SL_TEST_OFFSET + physical_page * 4096ULL, 4096);
			test_program_us[i] = div_u64(ktime_get_ns() - started, 1000);
			if (ret)
				break;
		}
		if (ret)
			goto cleanup;
		phase = "readback";
		ret = sl_test_check_block(chip, buffer, SL_TEST_BLOCK, true);
		verified = !ret;
	}
	if (!ret && verified && hold_for_stock) {
		neighbors = sl_test_check_block(chip, buffer, SL_TEST_BLOCK - 1, false);
		if (!neighbors)
			neighbors = sl_test_check_block(chip, buffer, SL_TEST_BLOCK + 1, false);
		if (!neighbors) {
			/* Explicit handoff mode only; errors still take local cleanup.
			 * The pre-cache stock root hook owns verification/final erase. */
			test_armed = false;
			phase = "held-for-stock";
			goto free;
		}
	}
cleanup:
	/* Cleanup is local to the module, so losing the SSH session cannot skip it. */
	cleanup = sl_test_reset(nfc);
	if (!cleanup)
		cleanup = sl_test_erase(chip, SL_TEST_OFFSET, SL_TEST_ERASESIZE, &cleanup_status);
	if (!cleanup) {
		cleanup = berlin_read_page(chip, buffer, 0);
		if (cleanup >= 0)
			cleanup = sl_test_check_block(chip, buffer, SL_TEST_BLOCK, false);
		empty = !cleanup;
	}
	test_armed = false;
	neighbors = sl_test_check_block(chip, buffer, SL_TEST_BLOCK - 1, false);
	if (!neighbors)
		neighbors = sl_test_check_block(chip, buffer, SL_TEST_BLOCK + 1, false);
	if (!ret && !cleanup && !neighbors)
		phase = "passed";
free:
	kfree(buffer);
report:
	scnprintf(test_result, sizeof(test_result),
		"phase=%s error=%d cleanup_error=%d neighbor_error=%d touched=%u "
		"readback_verified=%u final_empty=%u programs=%u status_before=%02x "
		"erase_status=%02x program_status=%02x cleanup_status=%02x",
		phase, ret, cleanup, neighbors, touched, verified, empty, test_program_count,
		initial_status, erase_status, program_status, cleanup_status);
	dev_info(nfc->dev, "bounded-write-test: %s\n", test_result);
}
