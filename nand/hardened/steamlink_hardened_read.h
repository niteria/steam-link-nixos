/* SPDX-License-Identifier: GPL-2.0-only */
#include "erased.h"
#include "transport.h"
static struct slh_transport slh_io;
module_param_named(controller_faulted, slh_io.faulted, int, 0400);
static u8 slh_reference_data[4096], slh_reference_metadata[32], slh_prime_data[4096];
static bool slh_reference_ready, slh_priming, slh_warming;
static unsigned long slh_raw_fallbacks;
module_param_named(raw_fallbacks, slh_raw_fallbacks, ulong, 0400);
static int berlin_read_page(struct nand_chip *chip, u8 *buffer, int page);

static int slh_hw_stop(void *context)
{
 struct nand_chip *chip = context;
 struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
 u32 control;
 int ret;
 writel(NDCR_ALL_INT, nfc->regs + NDCR);
 ret = readl_relaxed_poll_timeout_atomic(nfc->regs + NDCR, control,
                                       !(control & NDCR_ND_RUN), 1, POLL_TIMEOUT);
 if (!ret) writel(0xffffffff, nfc->regs + NDSR);
 else slh_io.faulted = 1;
 return ret;
}
static int slh_hw_configure(void *context, int bch)
{
 struct nand_chip *chip = context;
 struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
 u32 wanted = (48U << 24) | (bch ? NDECCCTRL_BCH_EN : 0);
 if (readl(nfc->regs + NDCR) & NDCR_ND_RUN) return -EBUSY;
 /* Preserve the Berlin strength field while bypassing BCH, as stock does.
  * Every mode boundary is stopped and read back, including the final restore. */
 writel(48U << 24, nfc->regs + NDECCCTRL);
 readl(nfc->regs + NDECCCTRL);
 writel(wanted, nfc->regs + NDECCCTRL);
 return readl(nfc->regs + NDECCCTRL) == wanted ? 0 : -EIO;
}
static int slh_hw_window(void *context, unsigned page, unsigned offset, unsigned length, unsigned char *raw)
{
 struct nand_chip *chip = context;
 struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
 int ret;
 if (page >= (1U << 20) || offset >= SLH_RAW_SIZE || !length || length > 2048 ||
     length > SLH_RAW_SIZE - offset || length % 8) return -EINVAL;
 ret = slh_hw_stop(chip);
 if (ret) return ret;
 /* Stock's raw path clears BOTH ECC_EN and SPARE_EN. LEN_OVRD explicitly
  * supplies every physical byte; automatic spare handling is inappropriate. */
 writel(0x0106f000 | NDCR_ALL_INT | NDCR_ND_RUN, nfc->regs + NDCR);
 ret = berlin_send_read_command(nfc, NDCB0_LEN_OVRD | NDCB0_DBC | NDCB0_ADDR_CYC(5) | 0x3000,
                               ((page & 0xffff) << 16) | offset, (page >> 16) & 0xff, length);
 if (!ret) ret = berlin_wait_status(nfc, NDSR_RDDREQ);
 if (!ret) {
  writel(NDSR_RDDREQ, nfc->regs + NDSR);
  for (unsigned i = 0; i < length; i += 8) ioread32_rep(nfc->regs + NDDB, raw + i, 2);
  ret = berlin_wait_status(nfc, NDSR_CMDD(0));
 }
 return ret;
}
static int slh_hw_reset(void *context)
{
 struct nand_chip *chip = context;
 struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
 int ret;
 writel(0x0106f000 | NDCR_ALL_INT | NDCR_ND_RUN, nfc->regs + NDCR);
 ret = berlin_send_read_command(nfc, (5U << 21) | 0xff, 0, 0, 0);
 if (!ret) ret = berlin_wait_status(nfc, NDSR_RDY(0));
 if (!ret) ret = berlin_wait_status(nfc, NDSR_CMDD(0));
 return ret;
}
static int slh_hw_prime(void *context)
{
 struct nand_chip *chip = context;
 u8 saved_metadata[32];
 int ret;
 if (!slh_reference_ready || slh_priming) return -EIO;
 memcpy(saved_metadata, chip->oob_poi, 32);
 slh_priming = true;
 /* Berlin's first BCH transaction after RAW/reset reports stale decoder
  * state (observed 00ff0511 even on known-good written page zero). Drain one
  * complete read into private scratch, then require a second, independent
  * decode to match the pre-RAW data AND metadata. Transport failures during
  * the drain still latch; no drain bytes reach the caller or ECC accounting. */
 slh_warming = true;
 ret = berlin_read_page(chip, slh_prime_data, 0);
 slh_warming = false;
 if (ret < 0) goto finished;
 ret = berlin_read_page(chip, slh_prime_data, 0);
finished:
 slh_priming = false;
 if (ret >= 0 && (memcmp(slh_prime_data, slh_reference_data, 4096) ||
                  memcmp(chip->oob_poi, slh_reference_metadata, 32))) {
  pr_err("steamlink hardened: BCH prime mismatch data=%d metadata=%d\n",
         memcmp(slh_prime_data, slh_reference_data, 4096),
         memcmp(chip->oob_poi, slh_reference_metadata, 32));
  ret = -EIO;
 }
 if (ret < 0) pr_err("steamlink hardened: BCH prime failed: %d\n", ret);
 memcpy(chip->oob_poi, saved_metadata, 32);
 return ret < 0 ? ret : 0;
}
static int slh_read_physical(struct nand_chip *chip, int page, u8 *raw)
{
 if (page < 0 || page >= (1 << 20)) return -EINVAL;
 if (!slh_reference_ready || slh_priming) return -EIO;
 slh_io.context = chip;
 slh_io.stop = slh_hw_stop;
 slh_io.configure = slh_hw_configure;
 slh_io.window = slh_hw_window;
 slh_io.reset = slh_hw_reset;
 slh_io.prime = slh_hw_prime;
 int ret = slh_raw_roundtrip(&slh_io, page, raw);
 if (ret) pr_err("steamlink hardened: RAW roundtrip page=%d failed: %d\n", page, ret);
 return ret;
}
static int slh_begin_bch(struct nand_chip *chip)
{
 struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
 int ret;
 if (slh_io.faulted) return -EIO;
 ret = slh_hw_stop(chip);
 if (!ret) ret = slh_hw_configure(chip, 1);
 if (ret) { slh_io.faulted = 1; return ret; }
 writel(0x0106f000 | NDCR_ALL_INT | NDCR_ND_RUN | NDCR_ECC_EN, nfc->regs + NDCR);
 return 0;
}
static int slh_finish_read(struct nand_chip *chip, u8 *buffer, int page, const u32 status[2])
{
 struct mtd_info *mtd = nand_to_mtd(chip);
 u8 *raw = NULL;
 int ret, maximum = 0;
 if (slh_warming) return 0;
 if (slh_priming && ((status[0] | status[1]) & NDSR_UNCERR)) {
  pr_err("steamlink hardened: BCH prime UNC status=%08x/%08x\n", status[0], status[1]);
  return -EIO;
 }
 if ((status[0] | status[1]) & NDSR_UNCERR) {
  raw = kmalloc(SLH_RAW_SIZE, GFP_KERNEL);
  if (!raw) return -ENOMEM;
  slh_raw_fallbacks++;
  ret = slh_read_physical(chip, page, raw);
  if (ret) { kfree(raw); return ret; }
 }
 for (unsigned chunk = 0; chunk < 2; chunk++) {
  int corrected = 0;
  if (status[chunk] & NDSR_UNCERR) {
   corrected = slh_normalize_erased(raw, buffer, chip->oob_poi, chunk);
   if (corrected < 0) {
    /* Keep both complete BCH data/OOB buffers. NAND core converts failed
     * statistics to EBADMSG with full retlen, as in the qualified contract. */
    mtd->ecc_stats.failed++;
    continue;
   }
  } else if (status[chunk] & NDSR_CORERR) {
   corrected = NDSR_ERRCNT(status[chunk]);
  }
  mtd->ecc_stats.corrected += corrected;
  maximum = max(maximum, corrected);
 }
 if (page == 0 && !((status[0] | status[1]) & NDSR_UNCERR)) {
  if (!slh_reference_ready) {
   if (!memchr_inv(buffer, 255, 4096)) { kfree(raw); return -EIO; }
   memcpy(slh_reference_data, buffer, 4096);
   memcpy(slh_reference_metadata, chip->oob_poi, 32);
   slh_reference_ready = true;
  } else if (memcmp(buffer, slh_reference_data, 4096) || memcmp(chip->oob_poi, slh_reference_metadata, 32)) {
   kfree(raw);
   return -EIO;
  }
 }
 kfree(raw);
 return maximum;
}
static int slh_raw_page(struct nand_chip *chip, u8 *buffer, int oob_required, int page)
{
 u8 *raw;
 int ret;
 if (slh_io.faulted) return -EIO;
 raw = kmalloc(SLH_RAW_SIZE, GFP_KERNEL);
 if (!raw) return -ENOMEM;
 marvell_nfc_select_target(chip, chip->cur_cs);
 ret = slh_read_physical(chip, page, raw);
 if (!ret) {
  if (!buffer) buffer = nand_get_data_buf(chip);
  /* Deliberately physical diagnostic stream, matching the existing tool. */
  memcpy(buffer, raw, 4096);
  memcpy(chip->oob_poi, raw + 4096, 224);
 }
 kfree(raw);
 return ret;
}
