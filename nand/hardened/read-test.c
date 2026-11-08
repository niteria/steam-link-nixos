/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "erased.h"
#include "transport.h"
struct fake { unsigned calls, fail, windows, bytes, resets; int stopped, mode; };
static int step(struct fake *f) { return ++f->calls == f->fail ? -110 : 0; }
static int stop(void *context)
{
 struct fake *f = context;
 int ret = step(f);
 if (!ret) f->stopped = 1;
 return ret;
}
static int configure(void *context, int bch)
{
 struct fake *f = context;
 assert(f->stopped);
 int ret = step(f);
 if (!ret) f->mode = bch;
 return ret;
}
static int window(void *context, unsigned page, unsigned offset, unsigned length, unsigned char *data)
{
 struct fake *f = context;
 assert(f->mode == 0 && page == 786432 && offset == f->bytes && length <= 2048 && !(length % 8));
 int ret = step(f);
 f->stopped = 0;
 if (!ret) { memset(data, 255, length); f->bytes += length; f->windows++; }
 return ret;
}
static int reset(void *context)
{
 struct fake *f = context;
 assert(f->stopped);
 int ret = step(f);
 if (!ret) { f->resets++; f->stopped = 0; }
 return ret;
}
static int prime(void *context)
{
 struct fake *f = context;
 assert(f->stopped && f->mode == 1);
 return step(f);
}
int main(void)
{
 unsigned char raw[SLH_RAW_SIZE];
 unsigned checks = 0;
 /* Every data, parity and metadata byte participates in its own ECC step. */
 for (unsigned byte = 0; byte < SLH_RAW_SIZE; byte++) {
  memset(raw, 255, sizeof(raw)); raw[byte] = 0xfe;
  assert(slh_erased_bits(raw, 0) == (byte < 2138 ? 1 : 0));
  assert(slh_erased_bits(raw, 1) == (byte >= 2138 && byte < 4308 ? 1 : 0));
  checks += 2;
 }
 const unsigned starts[] = {0, 2048, 2138, 4186, 4218};
 for (unsigned area = 0; area < 5; area++) for (unsigned bits = 0; bits <= 64; bits++) {
  memset(raw, 255, sizeof(raw));
  for (unsigned bit = 0; bit < bits; bit++) raw[starts[area] + bit / 8] &= ~(1U << (bit % 8));
  int chunk = area >= 2;
  assert(slh_erased_bits(raw, chunk) == (bits < 48 ? (int)bits : -1));
  assert(slh_erased_bits(raw, chunk ^ 1) == 0);
  checks += 2;
 }
 assert(slh_erased_bits(raw, 2) == -1);
 unsigned char data[4096], metadata[32];
 memset(raw, 255, sizeof(raw)); memset(data, 0x5a, sizeof(data)); memset(metadata, 0xa5, sizeof(metadata));
 memset(raw + 2048, 0, 6); /* Forty-eight parity-only zeros: reject chunk 0. */
 assert(slh_normalize_erased(raw, data, metadata, 0) == -1);
 for (unsigned i = 0; i < sizeof(data); i++) assert(data[i] == 0x5a);
 for (unsigned i = 0; i < sizeof(metadata); i++) assert(metadata[i] == 0xa5);
 assert(slh_normalize_erased(raw, data, metadata, 1) == 0);
 for (unsigned i = 0; i < sizeof(data); i++) assert(data[i] == (i < 2048 ? 0x5a : 255));
 for (unsigned i = 0; i < sizeof(metadata); i++) assert(metadata[i] == 255);
 /* Every callback boundary: successful roundtrip, each raw/restore failure,
  * and a second request after failure (must issue no more controller traffic). */
 for (unsigned fail = 0; fail <= 10; fail++) {
  struct fake f = {.fail = fail, .mode = 1};
  struct slh_transport io = {&f, stop, configure, window, reset, prime, 0};
  int ret = slh_raw_roundtrip(&io, 786432, raw);
  if (!fail) {
   assert(!ret && !io.faulted && f.mode == 1 && f.stopped && f.bytes == 4320 && f.windows == 3 && f.resets == 1);
  } else {
   assert(ret == -110 && io.faulted);
   unsigned calls = f.calls;
   assert(slh_raw_roundtrip(&io, 786432, raw) == -5 && f.calls == calls);
  }
  checks++;
 }
 printf("PASS: %u erased-layout/boundary/transport checks; parity-only partial program rejected, all restore faults latch\n", checks);
 return 0;
}
