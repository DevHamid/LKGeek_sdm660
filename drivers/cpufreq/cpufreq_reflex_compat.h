/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Reflex CPUFreq governor: the only kernel-version differences.
 * cpufreq_reflex.c is byte-identical across versions; this file is not.
 *
 * Ported to Linux 4.19 (MSM-4.19 / sdm660).  Differences from 6.x:
 *
 *  - cpufreq_governor has no .flags field: dynamic switching is expressed
 *    by the bool .dynamic_switching member instead of
 *    CPUFREQ_GOV_DYNAMIC_SWITCHING.
 *  - No cpufreq_driver_test_flags(), cpufreq_driver_has_adjust_perf(),
 *    cpufreq_driver_adjust_perf() or cpufreq_get_capacity_ref_freq():
 *    the adjust_perf fast path does not exist in 4.19, so the governor
 *    always takes the freq path and derives the reference frequency
 *    itself.
 *  - map_util_perf() does not exist; it is 1.25 * util, as upstream.
 *  - arch_scale_cpu_capacity() takes (sd, cpu).
 *  - get_cpu_idle_time() returns microseconds, not nanoseconds.
 *  - No cpufreq_pelt_decay(): PELT decay is reimplemented here with the
 *   4.19 kernel-internal accumulator (y^32 = 1/2).
 *  - No SCHED_FLAG_SUGOV: 4.19 cpufreq_schedutil.c uses the raw
 *    0x10000000 bit for the same purpose, and cpufreq_reflex.c hardcodes
 *    it.
 *  - Energy Model is the 4.19 one: em_cap_state table inline in
 *    em_perf_domain, em_pd_nr_cap_states() accessor, and no
 *    em_table RCU replacement -- the table is static for our lifetime.
 *  - No ACPI CPPC on this platform, so s*(0) is 0.
 */
#ifndef _CPUFREQ_REFLEX_COMPAT_H
#define _CPUFREQ_REFLEX_COMPAT_H

#include <linux/cpufreq.h>
#include <linux/energy_model.h>
#include <linux/kernel_stat.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/topology.h>
#include <linux/sched/types.h>


/* 4.19: cpufreq_driver.adjust_perf is absent, so this path is never taken. */
static inline bool cpufreq_driver_has_adjust_perf(void)
{
	return false;
}

/*
 * cpufreq_driver is static to drivers/cpufreq/cpufreq.c in 4.19, so the
 * flag test goes through a helper exported from there.
 */
extern bool cpufreq_driver_test_flags(u32 flags);

/*
 * 4.19 exports no cpufreq_get_effective_util(); kernel/sched provides the
 * same FREQUENCY_UTIL aggregation as rfx_cpu_util_freq().
 */
extern unsigned long rfx_cpu_util_freq(int cpu, unsigned long *bw_min,
				       unsigned long *bw_max);

static inline void rfx_adjust_perf(struct cpufreq_policy *policy,
				   unsigned long min_perf,
				   unsigned long target_perf,
				   unsigned long max_perf,
				   unsigned long capacity)
{
	/* Unreachable: the perf path is gated on has_adjust_perf(). */
	__cpufreq_driver_target(policy, target_perf, CPUFREQ_RELATION_L);
}

/* 1.25x headroom, same as upstream schedutil's map_util_perf(). */
static inline unsigned long map_util_perf(unsigned long util)
{
	return util + (util >> 2);
}

/*
 * 4.19 get_next_freq() derives its reference frequency from policy->cur or
 * cpuinfo.max_freq depending on arch_scale_freq_invariant().  Implement the
 * upstream cpufreq_get_capacity_ref_freq() semantics: without a
 * counter-based freq scale (this platform has none -- arm64 falls back to
 * arch_topology's frequency-invariant scale), the reference is the policy
 * maximum.
 */
static inline unsigned int
cpufreq_get_capacity_ref_freq(struct cpufreq_policy *policy)
{
	return policy->cpuinfo.max_freq;
}

/*
 * PELT decay: val * y^n with y^32 = 1/2, identical to decay_load() in
 * kernel/sched/pelt.c.  sched-pelt.h is private to kernel/sched, so the
 * table is carried here; the values are that generated table.
 */
static const u32 rfx_yN_inv[] = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};

#define RFX_LOAD_AVG_PERIOD	32

static inline unsigned long cpufreq_pelt_decay(unsigned long val, u64 delta_ns)
{
	u64 periods, n;

	if (!val)
		return 0;
	if (!delta_ns)
		return val;

	/* ns -> PELT periods (1ms each), rounded down */
	periods = div64_u64(delta_ns, 1024ULL * 1024ULL);
	if (periods >= RFX_LOAD_AVG_PERIOD * 64)
		return 0;

	n = periods;
	val >>= n / RFX_LOAD_AVG_PERIOD;
	n %= RFX_LOAD_AVG_PERIOD;

	return mul_u64_u32_shr((u64)val, rfx_yN_inv[n], 32);
}

#endif /* _CPUFREQ_REFLEX_COMPAT_H */
