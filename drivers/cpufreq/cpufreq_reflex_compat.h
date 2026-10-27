/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Reflex CPUFreq governor: 6.12 -> 4.9 compatibility shims.
 *
 * cpufreq_reflex.c is otherwise the upstream v0.4.0 source; every place
 * where the 4.9 cpufreq/sched/EM API differs from the 6.12 one is hidden
 * behind the helpers below.
 */
#ifndef _CPUFREQ_REFLEX_COMPAT_H
#define _CPUFREQ_REFLEX_COMPAT_H

#include <linux/bitops.h>
#include <linux/cpufreq.h>
#include <linux/math64.h>
#include <linux/types.h>

/*
 * 4.9 keeps arch_scale_freq_invariant() private to kernel/sched/sched.h,
 * while 6.12 exports it from <linux/sched/topology.h>. Mirror 4.9's own
 * definition so the governor sees the same answer schedutil does.
 */
#ifndef arch_scale_freq_invariant
#ifdef arch_scale_freq_capacity
#define arch_scale_freq_invariant()	(true)
#else
#define arch_scale_freq_invariant()	(false)
#endif
#endif

/*
 * 4.9 has no to_gov_attr_set(); the tunables kobject is embedded directly.
 */
static inline struct gov_attr_set *to_gov_attr_set(struct kobject *kobj)
{
	return container_of(kobj, struct gov_attr_set, kobj);
}

/*
 * 4.9's map_util_freq() lives in <linux/energy_model.h> behind
 * CONFIG_ENERGY_MODEL and map_util_perf() does not exist at all, so both
 * are re-implemented here. The formulas are the ones 6.12 uses:
 *
 *	map_util_freq(util, freq, cap) = (freq + (freq >> 2)) * util / cap
 *	map_util_perf(util)            = util + (util >> 2)
 */
static inline unsigned long rfx_map_util_freq(unsigned long util,
					      unsigned long freq,
					      unsigned long cap)
{
	return (freq + (freq >> 2)) * util / cap;
}

static inline unsigned long rfx_map_util_perf(unsigned long util)
{
	return util + (util >> 2);
}

/*
 * 4.9 has neither cpufreq_driver_adjust_perf() nor CPUFREQ_NEED_UPDATE_LIMITS,
 * so the adjust_perf fast path can never be selected at runtime
 * (cpufreq_driver_has_adjust_perf() below is always false). It still has to
 * compile, hence this empty stub.
 */
static inline void rfx_adjust_perf(struct cpufreq_policy *policy,
				   unsigned long min_perf,
				   unsigned long target_perf,
				   unsigned long max_perf,
				   unsigned long capacity)
{
}

/*
 * 4.9's struct cpufreq_driver has no ->flags, so there is no driver flag
 * to test against and nothing in the Reflex governor is gated on a driver
 * flag that 4.9 could express. CPUFREQ_NEED_UPDATE_LIMITS is defined here
 * purely so the code reads as it does upstream; the shim below never
 * reports it.
 */
#ifndef CPUFREQ_NEED_UPDATE_LIMITS
#define CPUFREQ_NEED_UPDATE_LIMITS	BIT(0)
#endif

static inline bool cpufreq_driver_test_flags(u16 flags)
{
	return false;
}

/* 4.9's struct cpufreq_driver has no adjust_perf callback. */
static inline bool cpufreq_driver_has_adjust_perf(void)
{
	return false;
}

/*
 * 4.9's struct cpufreq_policy has no ->transition_delay_us: the only knob
 * is the driver's transition_latency, in ns. Convert it the same way
 * drivers/cpufreq/cpufreq_governor.c does when it derives the sampling
 * rate, and never return 0 (an interval of 0 would disable rate limiting).
 */
static inline unsigned int
cpufreq_policy_transition_delay_us(struct cpufreq_policy *policy)
{
	unsigned int delay = policy->cpuinfo.transition_latency / 1000;

	return delay ? delay : 1;
}

/* 4.9's <linux/math64.h> has no DIV64_U64_ROUND_UP; the 6.12 definition. */
#ifndef DIV64_U64_ROUND_UP
#define DIV64_U64_ROUND_UP(ll, d) \
	({ u64 _old = (ll); u64 _div = (d); div64_u64(_old + _div - 1, _div); })
#endif

#endif /* _CPUFREQ_REFLEX_COMPAT_H */
