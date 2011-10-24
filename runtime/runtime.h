/* main header file
 * Copyright (C) 2005-2011 Red Hat Inc.
 * Copyright (C) 2005-2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _RUNTIME_H_
#define _RUNTIME_H_

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/mm.h>


#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15)
#if !defined (CONFIG_DEBUG_FS)  && !defined (CONFIG_DEBUG_FS_MODULE)
#error "DebugFS is required and was not found in the kernel."
#endif
#ifdef CONFIG_RING_BUFFER
#define STP_TRANSPORT_VERSION 3
#else
#define STP_TRANSPORT_VERSION 2
#endif
#else
/* older kernels have no debugfs and older version of relayfs. */
#define STP_TRANSPORT_VERSION 1
#endif

#ifndef stp_for_each_cpu
#define stp_for_each_cpu(cpu)  for_each_cpu_mask((cpu), cpu_possible_map)
#endif

#ifndef clamp
#define clamp(val, low, high)     min(max(low, val), high)
#endif

#ifndef clamp_t
#define clamp_t(type, val, low, high)   min_t(type, max_t(type, low, val), high)
#endif

static void _stp_dbug (const char *func, int line, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
static void _stp_error (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void _stp_warn (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

static void _stp_exit(void);



/* unprivileged user support */

#ifdef STAPCONF_TASK_UID
#define STP_CURRENT_EUID (current->euid)
#else
#define STP_CURRENT_EUID (task_euid(current))
#endif

#define is_myproc() (STP_CURRENT_EUID == _stp_uid)

#if STP_PRIVILEGE < STP_PR_STAPDEV
#define assert_is_myproc() do { \
  if (! is_myproc()) { \
    snprintf (CONTEXT->error_buffer, MAXSTRINGLEN, STAP_MSG_RUNTIME_H_01, \
             current->tgid, STP_CURRENT_EUID); \
    CONTEXT->last_error = CONTEXT->error_buffer; \
    goto out; \
  } } while (0)
#else
#define assert_is_myproc() do {} while (0)
#endif

/* Translate user privilege mask to text. */
static const char *privilege_to_text (int p) {
  if ((p & STP_PR_STAPDEV)) return "stapdev";
  if ((p & STP_PR_STAPUSR)) return "stapusr";
  return "unknown";
}

#include "debug.h"

/* atomic globals */
static atomic_t _stp_transport_failures = ATOMIC_INIT (0);

static struct
{
	atomic_t ____cacheline_aligned_in_smp seq;
} _stp_seq = { ATOMIC_INIT (0) };

#define _stp_seq_inc() (atomic_inc_return(&_stp_seq.seq))

/* dwarf unwinder only tested so far on i386, x86_64 and ppc64.
   Only define STP_USE_DWARF_UNWINDER when STP_NEED_UNWIND_DATA,
   as set through a pragma:unwind in one of the [u]context-unwind.stp
   functions. */
#if (defined(__i386__) || defined(__x86_64__) || defined(__powerpc64__))
#ifdef STP_NEED_UNWIND_DATA
#ifndef STP_USE_DWARF_UNWINDER
#define STP_USE_DWARF_UNWINDER
#endif
#endif
#endif

#include "alloc.c"
#include "print.h"
#include "string.c"
#include "arith.c"
#include "copy.c"
#include "regs.c"
#include "regs-ia64.c"

#include "task_finder.c"

#include "sym.c"
#ifdef STP_PERFMON
#include "perf.c"
#endif
#include "addr-map.c"

/* DWARF unwinder only tested so far on i386, x86_64 and ppc64.
   We only need to compile in the unwinder when both STP_NEED_UNWIND_DATA
   (set when a stap script defines pragma:unwind, as done in
   [u]context-unwind.stp) is defined and the architecture actually supports
   dwarf unwinding (as defined by STP_USE_DWARF_UNWINDER in runtime.h).  */
#ifdef STP_USE_DWARF_UNWINDER
#include "unwind.c"
#else
struct unwind_context { };
#endif

#ifdef module_param_cb			/* kernels >= 2.6.36 */
#define _STP_KERNEL_PARAM_ARG const struct kernel_param
#else
#define _STP_KERNEL_PARAM_ARG struct kernel_param
#endif

/* Support functions for int64_t module parameters. */
static int param_set_int64_t(const char *val, _STP_KERNEL_PARAM_ARG *kp)
{
  char *endp;
  long long ll;

  if (!val)
    return -EINVAL;

  /* simple_strtoll isn't exported... */
  if (*val == '-')
    ll = -simple_strtoull(val+1, &endp, 0);
  else
    ll = simple_strtoull(val, &endp, 0);

  if ((endp == val) || ((int64_t)ll != ll))
    return -EINVAL;

  *((int64_t *)kp->arg) = ll;
  return 0;
}

static int param_get_int64_t(char *buffer, _STP_KERNEL_PARAM_ARG *kp)
{
  return sprintf(buffer, "%lli", (long long)*((int64_t *)kp->arg));
}

#define param_check_int64_t(name, p) __param_check(name, p, int64_t)

#ifdef module_param_cb			/* kernels >= 2.6.36 */
static struct kernel_param_ops param_ops_int64_t = {
	.set = param_set_int64_t,
	.get = param_get_int64_t,
};
#endif
#undef _STP_KERNEL_PARAM_ARG

/************* Module Stuff ********************/

int init_module (void)
{
  return _stp_transport_init();
}

void cleanup_module(void)
{
  _stp_transport_close();
}

#define pseudo_atomic_cmpxchg(v, old, new) ({\
	int ret;\
	unsigned long flags;\
	local_irq_save(flags);\
	ret = atomic_read(v);\
	if (likely(ret == old))\
		atomic_set(v, new);\
	local_irq_restore(flags);\
	ret; })


#if defined(__ia64__) && defined(__GNUC__) && (__GNUC__ < 4)
/* Due to http://gcc.gnu.org/PR21838, old gcc on ia64 generates calls
   to __ia64_save_stack_nonlocal(), since our generated code uses
   __label__ constructs since PR11004.  This dummy declaration works
   around the undefined reference.  */
void __ia64_save_stack_nonlocal (void) { }
#endif

MODULE_LICENSE("GPL");

#endif /* _RUNTIME_H_ */
