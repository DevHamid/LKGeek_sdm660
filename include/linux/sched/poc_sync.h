/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Piece-Of-Cake (POC) CPU Selector: per-task learned WF_SYNC history.
 *
 * This is the data layout only.  The code that reads and updates it lives in
 * kernel/sched/poc_sync.h, which is included by fair.c and poc_selector.c;
 * both of those see the same struct through <linux/sched.h>.
 */
#ifndef _LINUX_SCHED_POC_SYNC_H
#define _LINUX_SCHED_POC_SYNC_H

#ifdef CONFIG_SCHED_POC_SELECTOR
/*
 * @hist:     one bit per sync wake, newest in bit 0.  1 = the waker lied
 *            (kept running instead of sleeping).
 * @chain:    one bit per sync wake, newest in bit 0.  1 = another sync wake
 *            was issued straight after this one.
 * @lie_log2: log2 of the waker's lie length, halved towards it on each lie.
 * @mark:     start time (task clock + 1) of the sync wake being resolved;
 *            0 when none is pending.
 * @sampled:  @hist and @chain have at least one valid entry.
 * @joined:   the wake being noted had others issued right behind it.
 * @on_waker_cpu: the wakee was placed on the waker's CPU.
 */
struct poc_sync {
	u16	hist;
	u16	chain;
	u8	lie_log2;
	u8	explore;	/* wraps every 16th wake so a waiter is seen */
	u64	mark;
	unsigned int	sampled:1,
			joined:1,
			on_waker_cpu:1;
};
#endif /* CONFIG_SCHED_POC_SELECTOR */

#endif /* _LINUX_SCHED_POC_SYNC_H */
