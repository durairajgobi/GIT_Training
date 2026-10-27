// SPDX-License-Identifier: GPL-2.0
/*
 * 099_kprobe.c - Tracing a kernel function with a kprobe and kretprobe.
 *
 * Default target: the function behind open()/openat() syscalls.
 * sudo insmod 099_kprobe.ko symbol=do_sys_openat2
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/sched.h>

static char symbol[KSYM_NAME_LEN] = "do_sys_openat2";
module_param_string(symbol, symbol, sizeof(symbol), 0444);

static atomic64_t hits = ATOMIC64_INIT(0);

static int pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	atomic64_inc(&hits);
	pr_info_ratelimited("kprobe: %s called by %s[%d]\n",
			    p->symbol_name, current->comm, current->pid);
	return 0;
}

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	long ret = regs_return_value(regs);

	if (ret < 0)
		pr_info_ratelimited("kretprobe: %s failed with %ld\n", symbol, ret);
	return 0;
}

static struct kprobe kp = {
	.pre_handler = pre_handler,
};

static struct kretprobe krp = {
	.handler   = ret_handler,
	.maxactive = 32,
};

static int __init kp_init(void)
{
	int ret;

	kp.symbol_name = symbol;
	krp.kp.symbol_name = symbol;

	ret = register_kprobe(&kp);
	if (ret) {
		pr_err("kprobe: register on %s failed %d\n", symbol, ret);
		return ret;
	}
	ret = register_kretprobe(&krp);
	if (ret) {
		unregister_kprobe(&kp);
		return ret;
	}
	pr_info("kprobe: planted at %s (%px)\n", symbol, kp.addr);
	return 0;
}

static void __exit kp_exit(void)
{
	unregister_kretprobe(&krp);
	unregister_kprobe(&kp);
	pr_info("kprobe: %lld hits, %d missed returns\n",
		atomic64_read(&hits), krp.nmissed);
}

module_init(kp_init);
module_exit(kp_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kprobe / kretprobe example");
