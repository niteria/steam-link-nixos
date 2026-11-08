/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_RAW_ROUNDTRIP_H
#define STEAMLINK_RAW_ROUNDTRIP_H
/* Shared orchestration used by the kernel adapter and fault-injection tests.
 * Callbacks return 0 or a negative errno. No callbacks can program/erase NAND. */
struct slh_transport {
 void *context;
 int (*stop)(void *);
 int (*configure)(void *, int bch);
 int (*window)(void *, unsigned page, unsigned offset, unsigned length, unsigned char *);
 int (*reset)(void *);
 int (*prime)(void *); /* Verify a known written BCH page before returning. */
 int faulted;
};
static inline int slh_raw_roundtrip(struct slh_transport *io, unsigned page, unsigned char *raw)
{
 int ret, restore;
 if (io->faulted) return -5; /* EIO, including standalone fault-model builds */
 ret = io->stop(io->context);
 if (ret) { io->faulted = 1; return ret; }
 ret = io->configure(io->context, 0);
 for (unsigned offset = 0; !ret && offset < 4320; ) {
  unsigned length = 4320 - offset > 2048 ? 2048 : 4320 - offset;
  ret = io->window(io->context, page, offset, length, raw + offset);
  offset += length;
 }
 /* A raw error must still try the ordered, bounded restore. Never retune a
  * controller which did not acknowledge stop, and never hide restore failure. */
 restore = io->stop(io->context);
 if (!restore) restore = io->reset(io->context);
 if (!restore) restore = io->stop(io->context);
 if (!restore) restore = io->configure(io->context, 1);
 if (!restore) restore = io->prime(io->context);
 if (ret || restore) io->faulted = 1;
 return restore ? restore : ret;
}
#endif
