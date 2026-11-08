// SPDX-License-Identifier: GPL-2.0-only
#ifndef STEAMLINK_AV_LAYOUT_H
#define STEAMLINK_AV_LAYOUT_H
/* Keep the SDK bank's physical origin. Only a declared no-map reservation may
 * back descriptors; the remainder of the old 32 MiB range belongs to Linux. */
static inline int ag_descriptor_range_valid(unsigned long long base,unsigned long long bytes)
{
    return base==0x1e000000ULL&&bytes>=0x00800000ULL&&bytes<=0x02000000ULL&&!(bytes%0x00400000ULL);
}
#endif
