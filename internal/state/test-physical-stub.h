/* SPDX-License-Identifier: GPL-2.0-only */
/* Host stand-in for steamlink_state_physical.h and the kernel/controller
 * interfaces module.h uses. Simulates blocks 2228..2231 with fault injection. */
#ifndef STEAMLINK_TEST_PHYSICAL_STUB_H
#define STEAMLINK_TEST_PHYSICAL_STUB_H
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "guard.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#include <sys/types.h>
#define __aligned(x) __attribute__((aligned(x)))
#define __maybe_unused __attribute__((unused))
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define pr_err(...) stub_printk(__VA_ARGS__)
#define pr_err_ratelimited(...) stub_printk(__VA_ARGS__)
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
static unsigned stub_log;
static char stub_last[256];
static void __attribute__((format(printf, 1, 2))) stub_printk(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(stub_last, sizeof(stub_last), fmt, ap);
	va_end(ap);
	stub_log++;
}
struct mtd_ecc_stats { unsigned failed; };
struct mtd_info { unsigned oobsize; struct mtd_ecc_stats ecc_stats; };
struct nand_chip { struct mtd_info mtd; u8 *oob_poi; int cur_cs; };
struct mtd_partition { const char *name; u64 offset, size; unsigned add_flags, mask_flags; };
#define MTD_SLC_ON_MLC_EMULATION 1
#define MTD_WRITEABLE 2
static inline struct mtd_info *nand_to_mtd(struct nand_chip *chip) { return &chip->mtd; }
static inline const void *memchr_inv(const void *p, int c, size_t n)
{
	const u8 *b = p;
	for (size_t i = 0; i < n; i++)
		if (b[i] != (u8)c) return b + i;
	return NULL;
}
static void marvell_nfc_select_target(struct nand_chip *chip, int cs) { (void)chip; (void)cs; }

#define SIM_FIRST 2228U
#define SIM_BLOCKS 4U
#define SIM_RAW 4320U
#define SIM_META 4186U
static u8 sim_raw[SIM_BLOCKS * 256][SIM_RAW];
static u8 sim_oob[SIM_BLOCKS * 256][224];
/* Injection knobs, reset by sim_reset(). */
static int inj_program_ret, inj_erase_ret, inj_stop_ret, inj_stop_after, inj_raw_read_ret;
static int inj_program_nowhere, inj_verify_corrupt, inj_verify_ecc;
static unsigned stub_stops;
static struct { int faulted; } slh_io;
static bool slh_reference_ready = true;
static void sim_reset(void)
{
	memset(sim_raw, 0xff, sizeof(sim_raw));
	memset(sim_oob, 0xff, sizeof(sim_oob));
	inj_program_ret = inj_erase_ret = inj_stop_ret = inj_stop_after = inj_raw_read_ret = 0;
	inj_program_nowhere = inj_verify_corrupt = inj_verify_ecc = 0;
	stub_stops = 0;
	slh_io.faulted = 0;
}
static u8 *sim_page(unsigned page)
{
	if (page / 256 < SIM_FIRST || page / 256 >= SIM_FIRST + SIM_BLOCKS) return NULL;
	return sim_raw[page - SIM_FIRST * 256];
}
static int slh_hw_stop(void *context)
{
	(void)context;
	if (inj_stop_ret && ++stub_stops > (unsigned)inj_stop_after) { slh_io.faulted = 1; return inj_stop_ret; }
	return 0;
}
static int slh_read_physical(void *context, unsigned page, u8 *raw)
{
	(void)context;
	if (inj_raw_read_ret) { slh_io.faulted = 1; return inj_raw_read_ret; }
	u8 *p = sim_page(page);
	if (!p) memset(raw, 0xff, SIM_RAW);
	else memcpy(raw, p, SIM_RAW);
	return 0;
}
static int berlin_read_page(struct nand_chip *chip, u8 *buffer, int page)
{
	u8 *p = sim_page((unsigned)page);
	if (!p) { memset(buffer, 0xff, 4096); memset(chip->oob_poi, 0xff, 224); return 0; }
	memcpy(buffer, p, 4096);
	memcpy(chip->oob_poi, sim_oob[page - SIM_FIRST * 256], 224);
	if (inj_verify_corrupt) buffer[7] ^= 0x10;
	if (inj_verify_ecc) chip->mtd.ecc_stats.failed++;
	return 0;
}
/* Names below are renamed by module.h's defines, as for the real header. */
static bool slc_armed(void) { return true; }
static int slc_write_page(struct nand_chip *chip, const u8 *data, int oob_required, int page)
{
	(void)oob_required;
	if (inj_program_ret) return inj_program_ret;
	if (inj_program_nowhere) return 0;
	u8 *p = sim_page((unsigned)page);
	if (!p) return -EINVAL;
	memcpy(p, data, 4096);
	memcpy(p + SIM_META, chip->oob_poi, 32);
	memcpy(sim_oob[page - SIM_FIRST * 256], chip->oob_poi, 224);
	return 0;
}
static int slc_erase_page(struct nand_chip *chip, unsigned row)
{
	(void)chip;
	if (inj_erase_ret) return inj_erase_ret;
	u8 *p = sim_page(row);
	if (p) {
		memset(p, 0xff, (size_t)256 * SIM_RAW);
		memset(sim_oob[row - SIM_FIRST * 256], 0xff, (size_t)256 * 224);
	}
	return 0;
}
static int slc_block_isbad(struct mtd_info *mtd, loff_t offset)
{
	(void)mtd;
	return (offset >> 20) == 90 || (offset >> 20) == 91;
}
static int slc_refuse_markbad(struct mtd_info *mtd, loff_t offset) { (void)mtd; (void)offset; return -EROFS; }
static const struct mtd_partition slc_trial_parts[] = { { "unused", 0, 0, 0, 0 } };
#endif
