#ifndef __REKERNEL_H
#define __REKERNEL_H

#include <linux/types.h>
#include <linux/sched/jobctl.h>
#include <linux/cgroup.h>
#include <linux/freezer.h>
#include <uapi/linux/android/binder.h>

#define MIN_USERAPP_UID	10000
#define MAX_SYSTEM_UID	2000
#define SYSTEM_APP_UID	1000
#define INTERFACETOKEN_BUFF_SIZE	140
#define PARCEL_OFFSET	16
#define LINE_ERROR	(-1)
#define LINE_SUCCESS	0

enum report_type {
	BINDER,
	SIGNAL,
#ifdef CONFIG_REKERNEL_NETWORK
	NETWORK,
#endif /* CONFIG_REKERNEL_NETWORK */
};
enum binder_type {
	REPLY,
	TRANSACTION,
	OVERFLOW,
};

static inline bool jobctl_frozen(struct task_struct *task)
{
	return ((task->jobctl & JOBCTL_TRAP_FREEZE) != 0);
}
static inline bool frozen_task_group(struct task_struct *task)
{
	return (jobctl_frozen(task) || cgroup_freezing(task));
}

extern void rekernel_report(int reporttype, int type, pid_t src_pid,
			    struct task_struct *src, pid_t dst_pid,
			    struct task_struct *dst, bool oneway,
			    struct binder_transaction_data *tr);
extern void binder_reply_handler(pid_t src_pid, struct task_struct *src,
				 pid_t dst_pid, struct task_struct *dst,
				 bool oneway, struct binder_transaction_data *tr);
extern void binder_trans_handler(pid_t src_pid, struct task_struct *src,
				 pid_t dst_pid, struct task_struct *dst,
				 bool oneway, struct binder_transaction_data *tr);
extern void binder_overflow_handler(pid_t src_pid, struct task_struct *src,
				    pid_t dst_pid, struct task_struct *dst,
				    bool oneway, struct binder_transaction_data *tr);

#endif /* __REKERNEL_H */
