/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of module.h's media/controller fault classification and
 * per-boot retirement, against the simulated physical layer. */
#include <assert.h>
#include <stdlib.h>
#include "module.h"

static u8 oob[224], page_data[4096];
static struct nand_chip chip = { .mtd = { .oobsize = 224 }, .oob_poi = oob };
static unsigned checks;

static void reset(void)
{
	sim_reset();
	state_faulted = false;
	memset(state_fresh, 0, sizeof(state_fresh));
	memset(state_retired, 0, sizeof(state_retired));
	memset(state_next, 0, sizeof(state_next));
	state_scans = state_programs = state_erases = state_media_faults = state_retirements = 0;
	state_nonce = 1;
	chip.mtd.ecc_stats.failed = 0;
	memset(page_data, 0x5a, sizeof(page_data));
	stub_log = 0;
}
static int write_rank(unsigned block, unsigned rank)
{
	memset(oob, 0xff, sizeof(oob));
	return slc_write_page(&chip, page_data, 0, (int)(block * 256 + sl_test_candidate_lower_page(rank)));
}
static int erase_block(unsigned block) { return slc_erase_page(&chip, block * 256); }
static int markbad(unsigned block) { return slc_refuse_markbad(&chip.mtd, (loff_t)block << 20); }
#define CHECK(x) do { assert(x); checks++; } while (0)

int main(void)
{
	/* Healthy writes, ordering and erase. */
	reset();
	CHECK(write_rank(2228, 0) == 0 && write_rank(2228, 1) == 0 && state_programs == 2);
	CHECK(write_rank(2228, 1) == -EPERM && !state_faulted);
	CHECK(erase_block(2228) == 0 && state_erases == 1 && write_rank(2228, 0) == 0);

	/* Media: program status FAIL fails only this program. */
	reset();
	inj_program_ret = -EIO;
	CHECK(write_rank(2228, 0) == -EIO && !state_faulted && state_media_faults == 1 &&
	      !strcmp(stub_last, "steamlink state: program status FAIL page=570368 block=2228; failing this operation\n"));
	CHECK(state_next[2228] == 0); /* rescanned before reuse */
	inj_program_ret = 0;
	CHECK(write_rank(2229, 0) == 0);

	/* Controller: a program timeout disables all later writes, not -EIO. */
	reset();
	inj_program_ret = -ETIMEDOUT;
	CHECK(write_rank(2228, 0) == -ETIMEDOUT && state_faulted &&
	      !strcmp(stub_last, "steamlink state: writes disabled: program page=570368 block=2228 ret=-110\n"));
	inj_program_ret = 0;
	CHECK(write_rank(2229, 0) == -EROFS && erase_block(2229) == -EROFS);

	/* Media: read-back mismatch and ECC failure. */
	reset();
	inj_verify_corrupt = 1;
	CHECK(write_rank(2228, 0) == -EIO && !state_faulted && state_media_faults == 1);
	reset();
	inj_verify_ecc = 1;
	CHECK(write_rank(2228, 0) == -EIO && !state_faulted);

	/* Controller: the page still reads erased after a "successful" program. */
	reset();
	inj_program_nowhere = 1;
	CHECK(write_rank(2228, 0) == -EREMOTEIO && state_faulted);

	/* Media: a stray bit in an unused upper page during the first-touch scan. */
	reset();
	sim_page(2228 * 256 + 4)[100] = 0xfe;
	CHECK(write_rank(2228, 0) == -EIO && !state_faulted && state_scans == 0);
	CHECK(write_rank(2229, 0) == 0);

	/* Media: the target page gained bits after the block was scanned. */
	reset();
	CHECK(write_rank(2228, 0) == 0);
	sim_page(2228 * 256 + sl_test_candidate_lower_page(1))[9] = 0x7f;
	CHECK(write_rank(2228, 1) == -EIO && !state_faulted);

	/* Controller: a successfully erased block is not empty when first written. */
	reset();
	CHECK(erase_block(2228) == 0);
	sim_page(2228 * 256 + sl_test_candidate_lower_page(5))[0] = 0;
	CHECK(write_rank(2228, 0) == -EREMOTEIO && state_faulted);

	/* Erase: status FAIL is media, a timeout is a controller fault. */
	reset();
	inj_erase_ret = -EIO;
	CHECK(erase_block(2228) == -EIO && !state_faulted && state_media_faults == 1);
	reset();
	inj_erase_ret = -ETIMEDOUT;
	CHECK(erase_block(2228) == -ETIMEDOUT && state_faulted);
	reset();
	inj_erase_ret = -EREMOTEIO; /* unexpected NAND status */
	CHECK(erase_block(2228) == -EREMOTEIO && state_faulted);

	/* Controller: stop failures before/after program and raw-read transport errors. */
	reset();
	inj_stop_ret = -ETIMEDOUT;
	CHECK(write_rank(2228, 0) == -ETIMEDOUT && state_faulted);
	reset();
	inj_stop_ret = -ETIMEDOUT;
	inj_stop_after = 1;
	CHECK(write_rank(2228, 0) == -ETIMEDOUT && state_faulted);
	reset();
	inj_raw_read_ret = -EIO;
	CHECK(write_rank(2228, 0) == -EREMOTEIO && state_faulted);

	/* Retirement: accepted in RAM, reported bad and refused for I/O. */
	reset();
	CHECK(!slc_block_isbad(&chip.mtd, (loff_t)2230 << 20));
	CHECK(markbad(2230) == 0 && state_retirements == 1 &&
	      !strcmp(stub_last, "steamlink state: block 2230 retired for this boot at UBI request\n"));
	CHECK(markbad(2230) == 0 && state_retirements == 1);
	CHECK(slc_block_isbad(&chip.mtd, (loff_t)2230 << 20) == 1);
	CHECK(write_rank(2230, 0) == -EIO && erase_block(2230) == -EIO && !state_faulted);
	CHECK(slc_block_isbad(&chip.mtd, (loff_t)90 << 20) == 1); /* factory-bad passthrough */
	CHECK(slc_refuse_markbad(&chip.mtd, ((loff_t)2230 << 20) + 4096) == -EINVAL);
	/* Retirement follows the build's writable aperture (state or reclaim policy). */
	CHECK(markbad(100) == (sl_trial_erase_allowed(100ULL << 20, 1048576) ? 0 : -EINVAL));
	CHECK(markbad(2227) == -EINVAL && markbad(4086) == -EINVAL);
	CHECK(slc_refuse_markbad(&chip.mtd, -1) == -EINVAL);

	/* Retirement is refused once controller state is unknown. */
	reset();
	state_faulted = true;
	CHECK(markbad(2231) == -EROFS && !state_retired[2231]);
	reset();
	slh_io.faulted = 1;
	CHECK(markbad(2231) == -EROFS);

	/* Policy refusals are unchanged and do not latch. */
	reset();
	CHECK(slc_write_page(&chip, page_data, 0, 2228 * 256 + 4) == -EROFS && !state_faulted);
	CHECK(slc_erase_page(&chip, 2227 * 256) == -EROFS && !state_faulted);
	state_nonce = 0;
	CHECK(write_rank(2228, 0) == -EROFS);

	printf("PASS: %u write-fault classification/retirement checks\n", checks);
	return 0;
}
