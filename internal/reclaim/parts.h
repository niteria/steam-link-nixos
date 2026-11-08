/* SPDX-License-Identifier: GPL-2.0-only */
static const struct mtd_partition slc_trial_parts[]={
 {.name="steamlink-reclaim-cache",.offset=2228ULL<<20,.size=1858ULL<<20,.add_flags=MTD_SLC_ON_MLC_EMULATION},
 {.name="steamlink-reclaim-sysconf",.offset=20ULL<<20,.size=512ULL<<20,.add_flags=MTD_SLC_ON_MLC_EMULATION},
 {.name="steamlink-reclaim-factory",.offset=756ULL<<20,.size=384ULL<<20,.add_flags=MTD_SLC_ON_MLC_EMULATION},
 {.name="steamlink-reclaim-physical-ro",.offset=0,.size=4096ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-reclaim-boot-ro",.offset=2164ULL<<20,.size=64ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-reclaim-stock-low-ro",.offset=0,.size=20ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-reclaim-identity-boot-ro",.offset=532ULL<<20,.size=224ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-reclaim-stock-root-ro",.offset=1140ULL<<20,.size=1024ULL<<20,.mask_flags=MTD_WRITEABLE},
 {.name="steamlink-reclaim-metadata-bbt-ro",.offset=4086ULL<<20,.size=10ULL<<20,.mask_flags=MTD_WRITEABLE},
};
