/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#include "irq-rate.h"

int main(void)
{
    const unsigned long hz = 1000;
    struct slpe_irq_rate rate = {0};
    /* Real startup convention: INITIAL_JIFFIES = -300*HZ. Start the PE at
     * uptime100s and run normal125Hz audio through boot-time unsigned wrap. */
    const unsigned long start = 0UL - 200 * hz;
    for (unsigned long event = 0; event < 50000; event++) {
        assert(!slpe_irq_rate_exceeded(&rate, start + event * 8, hz));
        assert(rate.count <= 125);
    }
    /* A real >10000/s storm must still fail-stop, including near wrap. */
    rate = (struct slpe_irq_rate){0};
    for (unsigned int event = 0; event < 10000; event++)
        assert(!slpe_irq_rate_exceeded(&rate, ULONG_MAX - 500, hz));
    assert(slpe_irq_rate_exceeded(&rate, ULONG_MAX - 500, hz));
    assert(!slpe_irq_rate_exceeded(&rate, 499, hz));
    assert(rate.count == 1);
    /* At the deadline, an ordinary new window starts without a false trip. */
    rate = (struct slpe_irq_rate){0};
    assert(!slpe_irq_rate_exceeded(&rate, 0, hz));
    assert(!slpe_irq_rate_exceeded(&rate, hz - 1, hz));
    assert(rate.count == 2);
    assert(!slpe_irq_rate_exceeded(&rate, hz, hz));
    assert(rate.count == 1);
    puts("PASS: normal125Hz across initial negative jiffies/wrap, real storm rejection, exact window reset");
}
