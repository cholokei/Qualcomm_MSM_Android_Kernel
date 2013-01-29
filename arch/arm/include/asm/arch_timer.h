#ifndef __ASMARM_ARCH_TIMER_H
#define __ASMARM_ARCH_TIMER_H

#include <asm/errno.h>
#include <linux/clocksource.h>
#include <asm/errno.h>

struct arch_timer {
	struct resource	res[3];
};

#ifdef CONFIG_ARM_ARCH_TIMER
int arch_timer_of_register(void);
int arch_timer_sched_clock_init(void);
struct timecounter *arch_timer_get_timecounter(void);
cycle_t arch_counter_get_cntpct(void);
#else
static inline int arch_timer_of_register(void)
{
	return -ENXIO;
}

static inline int arch_timer_sched_clock_init(void)
{
	return -ENXIO;
}

static inline struct timecounter *arch_timer_get_timecounter(void)
{
	return NULL;
}

static inline cycle_t arch_counter_get_cntpct(void)
{
	return 0;
}
#endif

#endif
