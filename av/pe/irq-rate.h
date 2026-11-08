/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_PE_IRQ_RATE_H
#define STEAMLINK_PE_IRQ_RATE_H

struct slpe_irq_rate {
    unsigned long until;
    unsigned int count;
};

/* Linux starts jiffies before unsigned wrap, not at zero. Initialize on the
 * first event: comparing that initial timestamp against a zero deadline would
 * otherwise accumulate normal interrupts until the first jiffies wrap. */
static inline bool slpe_irq_rate_exceeded(struct slpe_irq_rate *rate,
                                         unsigned long now,
                                         unsigned long ticks_per_second)
{
    if (!rate->count || time_after_eq(now, rate->until)) {
        rate->until = now + ticks_per_second;
        rate->count = 0;
    }
    return ++rate->count > 10000;
}

#endif
