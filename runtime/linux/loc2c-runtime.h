/* target operations in the Linux kernel mode
 * Copyright (C) 2005-2016 Red Hat Inc.
 * Copyright (C) 2005-2007 Intel Corporation.
 * Copyright (C) 2007 Quentin Barnes.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _LINUX_LOC2C_RUNTIME_H_
#define _LINUX_LOC2C_RUNTIME_H_

#ifdef STAPCONF_LINUX_UACCESS_H
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif
#include <linux/types.h>
#define intptr_t long
#define uintptr_t unsigned long

#include "../loc2c-runtime.h"


#ifndef STAPCONF_PAGEFAULT_DISABLE  /* before linux commit a866374a */
#define pagefault_disable() preempt_disable()
#define pagefault_enable() preempt_enable_no_resched()
#endif


#define k_fetch_register(regno) \
  pt_regs_fetch_register(c->kregs, regno)
#define k_store_register(regno, value) \
  pt_regs_store_register(c->kregs, regno, value)


/* PR 10601: user-space (user_regset) register access.
   Needs arch specific code, only i386 and x86_64 for now.  */
#if ((defined(STAPCONF_REGSET) || defined(STAPCONF_UTRACE_REGSET)) \
     && (defined (__i386__) || defined (__x86_64__)))

#if defined(STAPCONF_REGSET)
#include <linux/regset.h>
#endif

#if defined(STAPCONF_UTRACE_REGSET)
#include <linux/tracehook.h>
/* adapt new names to old decls */
#define user_regset_view utrace_regset_view
#define user_regset utrace_regset
#define task_user_regset_view utrace_native_view

#else // PR13489, inodes-uprobes export kludge
#if !defined(STAPCONF_TASK_USER_REGSET_VIEW_EXPORTED)
// First typedef from the original decl, then #define it as a typecasted call.
// NB: not all archs actually have the function, but the decl is universal in regset.h
typedef typeof(&task_user_regset_view) task_user_regset_view_fn;
/* Special macro to tolerate the kallsyms function pointer being zero. */
#define task_user_regset_view(t) (kallsyms_task_user_regset_view ? \
                                  (* (task_user_regset_view_fn)(kallsyms_task_user_regset_view))((t)) : \
                                  NULL)
#endif
#endif

struct usr_regset_lut {
  char *name;
  unsigned rsn;
  unsigned pos;
};


/* DWARF register number -to- user_regset bank/offset mapping table.
   The register numbers come from the processor-specific ELF documents.
   The user-regset bank/offset values come from kernel $ARCH/include/asm/user*.h
   or $ARCH/kernel/ptrace.c. */
#if defined (__i386__) || defined (__x86_64__)
static const struct usr_regset_lut url_i386[] = {
  { "ax", NT_PRSTATUS, 6*4 },
  { "cx", NT_PRSTATUS, 1*4 },
  { "dx", NT_PRSTATUS, 2*4 },
  { "bx", NT_PRSTATUS, 0*4 },
  { "sp", NT_PRSTATUS, 15*4 },
  { "bp", NT_PRSTATUS, 5*4 },
  { "si", NT_PRSTATUS, 3*4 },
  { "di", NT_PRSTATUS, 4*4 },
  { "ip", NT_PRSTATUS, 12*4 },
};
#endif

#if defined (__x86_64__)
static const struct usr_regset_lut url_x86_64[] = {
  { "rax", NT_PRSTATUS, 10*8 },
  { "rdx", NT_PRSTATUS, 12*8 },
  { "rcx", NT_PRSTATUS, 11*8 },
  { "rbx", NT_PRSTATUS, 5*8 },
  { "rsi", NT_PRSTATUS, 13*8 },
  { "rdi", NT_PRSTATUS, 14*8 },
  { "rbp", NT_PRSTATUS, 4*8 },
  { "rsp", NT_PRSTATUS, 19*8 },
  { "r8", NT_PRSTATUS, 9*8 },
  { "r9", NT_PRSTATUS, 8*8 }, 
  { "r10", NT_PRSTATUS, 7*8 },  
  { "r11", NT_PRSTATUS, 6*8 }, 
  { "r12", NT_PRSTATUS, 3*8 }, 
  { "r13", NT_PRSTATUS, 2*8 }, 
  { "r14", NT_PRSTATUS, 1*8 }, 
  { "r15", NT_PRSTATUS, 0*8 }, 
  { "rip", NT_PRSTATUS, 16*8 }, 
  /* XXX: SSE registers %xmm0-%xmm7 */ 
  /* XXX: SSE2 registers %xmm8-%xmm15 */
  /* XXX: FP registers %st0-%st7 */
  /* XXX: MMX registers %mm0-%mm7 */
};
#endif
/* XXX: insert other architectures here. */


static u32 ursl_fetch32 (const struct usr_regset_lut* lut, unsigned lutsize, int e_machine, unsigned regno)
{
  u32 value = ~0;
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->get)(current, rs, pos, count, & value, NULL);
        WARN_ON (rc);
        /* success */
        goto out;
      }
  WARN_ON (1); /* did not find appropriate regset! */
  
 out:
  return value;
}


static void ursl_store32 (const struct usr_regset_lut* lut,unsigned lutsize,  int e_machine, unsigned regno, u32 value)
{
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->set)(current, rs, pos, count, & value, NULL);
        WARN_ON (rc);
        /* success */
        goto out;
      }
  WARN_ON (1); /* did not find appropriate regset! */
  
 out:
  return;
}


static u64 ursl_fetch64 (const struct usr_regset_lut* lut, unsigned lutsize, int e_machine, unsigned regno)
{
  u64 value = ~0;
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  if (!rsv) goto out;
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->get)(current, rs, pos, count, & value, NULL);
        if (rc)
          goto out;
        /* success */
        return value;
      }
 out:
  printk (KERN_WARNING "process %d mach %d regno %d not available for fetch.\n", current->tgid, e_machine, regno);
  return value;
}


static void ursl_store64 (const struct usr_regset_lut* lut,unsigned lutsize,  int e_machine, unsigned regno, u64 value)
{
  const struct user_regset_view *rsv = task_user_regset_view(current); 
  unsigned rsi;
  int rc;
  unsigned rsn;
  unsigned pos;
  unsigned count;

  WARN_ON (!rsv);
  if (!rsv) goto out;
  WARN_ON (regno >= lutsize);
  if (regno >= lutsize) goto out;
  if (rsv->e_machine != e_machine) goto out;

  rsn = lut[regno].rsn;
  pos = lut[regno].pos;
  count = sizeof(value);

  for (rsi=0; rsi<rsv->n; rsi++)
    if (rsv->regsets[rsi].core_note_type == rsn)
      {
        const struct user_regset *rs = & rsv->regsets[rsi];
        rc = (rs->set)(current, rs, pos, count, & value, NULL);
        if (rc)
          goto out;
        /* success */
        return;
      }
  
 out:
  printk (KERN_WARNING "process %d mach %d regno %d not available for store.\n", current->tgid, e_machine, regno);
  return;
}


#if defined (__i386__)

#define u_fetch_register(regno) ursl_fetch32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno)
#define u_store_register(regno,value) ursl_store32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno, value)

#elif defined (__x86_64__)

#define u_fetch_register(regno) (_stp_is_compat_task() ? ursl_fetch32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno) : ursl_fetch64(url_x86_64, ARRAY_SIZE(url_x86_64), EM_X86_64, regno))
#define u_store_register(regno,value)  (_stp_is_compat_task() ? ursl_store32(url_i386, ARRAY_SIZE(url_i386), EM_386, regno, value) : ursl_store64(url_x86_64, ARRAY_SIZE(url_x86_64), EM_X86_64, regno, value))

#endif

#else /* ! STAPCONF_REGSET */

/* Downgrade to pt_dwarf_register access. */

#define u_store_register(regno, value) \
  pt_regs_store_register(c->uregs, regno, value)

/* If we're in a 32/31-bit task in a 64-bit kernel, we need to emulate
 * 32-bitness by masking the output of pt_regs_fetch_register() */
#ifndef CONFIG_COMPAT
#define u_fetch_register(regno) \
  pt_regs_fetch_register(c->uregs, regno)
#else
#define u_fetch_register(regno) \
  (_stp_is_compat_task() ? (0xffffffff & pt_regs_fetch_register(c->uregs, regno)) \
                         : pt_regs_fetch_register(c->uregs, regno))
#endif

#endif /* STAPCONF_REGSET */


/* The deref and store_deref macros are called to safely access
   addresses in the probe context.  These macros are used only for
   kernel addresses.  The macros must handle bogus addresses here
   gracefully (as from corrupted data structures, stale pointers,
   etc), by doing a "goto deref_fault".

   On most machines, the asm/uaccess.h macros __get_user and
   __put_user macros do exactly the low-level work we need to access
   memory with fault handling, and are not actually specific to
   user-address access at all.  */

/*
 * On most platforms, __get_user_inatomic() and __put_user_inatomic()
 * are defined, which are the same as __get_user() and __put_user(),
 * but without a call to might_sleep(). Since we're disabling page
 * faults when we read, we want to use the 'inatomic' variants when
 * available.
 */
#ifdef __get_user_inatomic
#define __stp_get_user __get_user_inatomic
#else
#define __stp_get_user __get_user
#endif
#ifdef __put_user_inatomic
#define __stp_put_user __put_user_inatomic
#else
#define __stp_put_user __put_user
#endif

/*
 * Some arches (like aarch64) don't have __get_user_bad() or
 * __put_user_bad(), so use BUILD_BUG() instead.
 */
#ifdef BUILD_BUG
#define __stp_get_user_bad BUILD_BUG
#define __stp_put_user_bad BUILD_BUG
#else
#define __stp_get_user_bad __get_user_bad
#define __stp_put_user_bad __put_user_bad
#endif


/* 
 * __stp_deref_nocheck(): reads a simple type from a
 * location with no address sanity checking.
 *
 * value: read the simple type into this variable
 * size: number of bytes to read
 * addr: address to read from
 *
 * Note that the caller *must* check the address for validity and do
 * any other checks necessary. This macro is designed to be used as
 * the base for the other macros more suitable for the rest of the
 * code to use. Note that the caller is also responsible for ensuring
 * that the kernel doesn't pagefault while reading.
 */

#define __stp_deref_nocheck(value, size, addr)				      \
  ({									      \
    int _err = -EFAULT;							      \
    switch (size)							      \
      {									      \
      case 1: { u8 _b; _err = __stp_get_user(_b, ((u8 *)(uintptr_t)(addr)));  \
		(value) = _b; }						      \
	break;								      \
      case 2: { u16 _w; _err = __stp_get_user(_w, ((u16 *)(uintptr_t)(addr))); \
		(value) = _w; }						      \
	break;								      \
      case 4: { u32 _l; _err = __stp_get_user(_l, ((u32 *)(uintptr_t)(addr))); \
		(value) = _l; }						      \
	break;								      \
      case 8: { u64 _q; _err = __stp_get_user(_q, ((u64 *)(uintptr_t)(addr))); \
		(value) = _q; }						      \
	break;								      \
      default:								      \
 	__stp_get_user_bad();					      	      \
      }									      \
    _err;								      \
  })


/* 
 * _stp_deref(): safely read a simple type from memory
 *
 * size: number of bytes to read
 * addr: address to read from
 * seg: memory segment to use, either kernel (KERN_DS) or user
 * (USER_DS)
 * 
 * The macro returns the value read. If this macro gets an error while
 * trying to read memory, a DEREF_FAULT will occur. Note that the
 * kernel will not pagefault when trying to read the memory.
 */

#define _stp_deref(size, addr, seg)                                           \
  ({									      \
    int _bad = 0;							      \
    intptr_t _v = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(seg);                                                              \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_READ, (const unsigned long)(addr), (size)))    \
      _bad = -EFAULT;                                                         \
    else                                                                      \
      _bad = __stp_deref_nocheck(_v, (size), (addr));			      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    if (_bad)                                                                 \
      DEREF_FAULT(addr);						      \
    _v;									      \
  })


/* 
 * _stp_deref_nofault(): safely read a simple type from memory without
 * a DEREF_FAULT on error
 *
 * value: read the simple type into this variable
 * size: number of bytes to read
 * addr: address to read from
 * seg: memory segment to use, either kernel (KERN_DS) or user
 * (USER_DS)
 * 
 * If this macro gets an error while trying to read memory, nonzero is
 * returned. On success, 0 is return. Note that the kernel will not
 * pagefault when trying to read the memory.
 */

#define _stp_deref_nofault(value, size, addr, seg)			      \
  ({									      \
    int _bad = 0;							      \
    intptr_t _v = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(seg);                                                              \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_READ, (const unsigned long)(addr), (size)))    \
      _bad = -EFAULT;							      \
    else                                                                      \
      _bad = __stp_deref_nocheck((value), (size), (addr));		      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    _bad;								      \
  })


/* 
 * __stp_store_deref_nocheck(): writes a simple type to a location
 * with no address sanity checking.
 *
 * size: number of bytes to write
 * addr: address to write to
 * value: read the simple type from this variable
 *
 * Note that the caller *must* check the address for validity and do
 * any other checks necessary. This macro is designed to be used as
 * the base for the other macros more suitable for the rest of the
 * code to use. Note that the caller is also responsible for ensuring
 * that the kernel doesn't pagefault while writing.
 */

#define __stp_store_deref_nocheck(size, addr, value)			      \
  ({									      \
    int _err = -EFAULT;							      \
    switch (size)							      \
      {									      \
      case 1: _err = __stp_put_user(((u8)(value)), ((u8 *)(uintptr_t)(addr)));\
        break;								      \
      case 2: _err = __stp_put_user(((u16)(value)), ((u16 *)(uintptr_t)(addr)));\
        break;								      \
      case 4: _err = __stp_put_user(((u32)(value)), ((u32 *)(uintptr_t)(addr)));\
        break;								      \
      case 8: _err = __stp_put_user(((u64)(value)), ((u64 *)(uintptr_t)(addr)));\
        break;								      \
      default:								      \
	__stp_put_user_bad();						      \
      }									      \
    _err;								      \
  })


/* 
 * _stp_store_deref(): safely write a simple type to memory
 *
 * size: number of bytes to write
 * addr: address to write to
 * value: read the simple type from this variable
 * seg: memory segment to use, either kernel (KERN_DS) or user
 * (USER_DS)
 * 
 * The macro has no return value. If this macro gets an error while
 * trying to write, a STORE_DEREF_FAULT will occur. Note that the
 * kernel will not pagefault when trying to write the memory.
 */

#define _stp_store_deref(size, addr, value, seg)                              \
  ({									      \
    int _bad = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(seg);                                                              \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_WRITE, (const unsigned long)(addr), (size)))   \
      _bad = -EFAULT;                                                         \
    else                                                                      \
      _bad = __stp_store_deref_nocheck((size), (addr), (value));	      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    if (_bad)								      \
      STORE_DEREF_FAULT(addr);						      \
  })


/* Map kderef/uderef to the generic segment-aware deref macros. */ 

#ifndef kderef
#define kderef(s,a) _stp_deref(s,a,KERNEL_DS)
#endif

#ifndef store_kderef
#define store_kderef(s,a,v) _stp_store_deref(s,a,v,KERNEL_DS)
#endif

#ifndef uderef
#define uderef(s,a) _stp_deref(s,a,USER_DS)
#endif

#ifndef store_uderef
#define store_uderef(s,a,v) _stp_store_deref(s,a,v,USER_DS)
#endif

#if defined (__i386__) || defined (__arm__)

/* x86 and arm can't do 8-byte put/get_user_asm, so we have to split it */

#define __Xread(ptr, Xderef)				\
  ((sizeof(*(ptr)) == 8) ?				\
       *(typeof(ptr))&(u32[2]) {			\
	 (u32) Xderef(4, &((u32 *)(ptr))[0]),		\
	 (u32) Xderef(4, &((u32 *)(ptr))[1]) }		\
     : (typeof(*(ptr))) Xderef(sizeof(*(ptr)), (ptr)))

#define __Xwrite(ptr, value, store_Xderef)				     \
  ({									     \
    if (sizeof(*(ptr)) == 8) {						     \
      union { typeof(*(ptr)) v; u32 l[2]; } _kw;			     \
      _kw.v = (typeof(*(ptr)))(value);					     \
      store_Xderef(4, &((u32 *)(ptr))[0], _kw.l[0]);			     \
      store_Xderef(4, &((u32 *)(ptr))[1], _kw.l[1]);			     \
    } else								     \
      store_Xderef(sizeof(*(ptr)), (ptr), (long)(typeof(*(ptr)))(value));    \
  })

#else

#define __Xread(ptr, Xderef) \
  ( (typeof(*(ptr))) Xderef(sizeof(*(ptr)), (ptr)) )
#define __Xwrite(ptr, value, store_Xderef) \
  ( store_Xderef(sizeof(*(ptr)), (ptr), (long)(typeof(*(ptr)))(value)) )

#endif

#define kread(ptr) __Xread((ptr), kderef)
#define uread(ptr) __Xread((ptr), uderef)

#define kwrite(ptr, value) __Xwrite((ptr), (value), store_kderef)
#define uwrite(ptr, value) __Xwrite((ptr), (value), store_uderef)


/* Dereference a kernel buffer ADDR of size MAXBYTES. Put the bytes in
 * address DST (which can be NULL).
 *
 * This function is useful for reading memory when the size isn't a
 * size that kderef() handles.  This function is very similar to
 * kderef_string(), but kderef_buffer() doesn't quit when finding a
 * '\0' byte or append a '\0' byte.
 */

#define kderef_buffer(dst, addr, maxbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    unsigned char _c;							      \
    char *_d = (dst);							      \
    int _bad = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(KERNEL_DS);							      \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_READ, (const unsigned long)(addr), (maxbytes))) \
      _bad = 1;                                                               \
    else                                                                      \
      for (_len = (maxbytes), _addr = (uintptr_t)(addr);		      \
	   _len > 1; --_len, ++_addr)					      \
        {								      \
	  _bad = __stp_deref_nocheck(_c, 1, _addr);			      \
	  if (_bad)							      \
	    break;							      \
	  else if (_d)							      \
	    *_d++ = _c;							      \
	}								      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    if (_bad)                                                                 \
      DEREF_FAULT(addr);						      \
    (dst);								      \
  })

/* The following is for kernel strings, see the uconversions.stp
   tapset for user_string functions. */


#define kderef_string(dst, addr, maxbytes)				      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    unsigned char _c;							      \
    char *_d = (dst);							      \
    int _bad = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(KERNEL_DS);							      \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_READ, (const unsigned long)(addr), (maxbytes))) \
      _bad = 1;                                                               \
    else                                                                      \
      {									      \
	for (_len = (maxbytes), _addr = (uintptr_t)(addr);		      \
	     _len > 1; --_len, ++_addr)					      \
	  {								      \
	    _bad = __stp_deref_nocheck(_c, 1, _addr);			      \
	    if (_bad || _c == '\0')					      \
	      break;							      \
	    else if (_d)						      \
	      *_d++ = _c;						      \
	  }								      \
	if (!_bad && _d)						      \
	  *_d = '\0';							      \
      }									      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    if (_bad)                                                                 \
      DEREF_FAULT(addr);						      \
    (dst);								      \
  })


/* 
 * _stp_store_deref_string(): safely write a string to memory
 *
 * src: source string
 * addr: address to write to
 * maxbytes: maximum number of bytes to write
 * seg: memory segment to use, either kernel (KERN_DS) or user
 * (USER_DS)
 * 
 * The macro has no return value. If this macro gets an error while
 * trying to write, a STORE_DEREF_FAULT will occur. Note that the
 * kernel will not pagefault when trying to write the memory.
 */

#define _stp_store_deref_string(src, addr, maxbytes, seg)		      \
  ({									      \
    uintptr_t _addr;							      \
    size_t _len;							      \
    char *_s = (src);							      \
    int _bad = 0;							      \
    mm_segment_t _oldfs = get_fs();                                           \
    set_fs(seg);                                                              \
    pagefault_disable();                                                      \
    if (lookup_bad_addr(VERIFY_WRITE, (const unsigned long)(addr), (maxbytes))) \
      _bad = 1;                                                               \
    else                                                                      \
      {									      \
	for (_len = (maxbytes), _addr = (uintptr_t)(addr);		      \
	     _len > 1 && _s && *_s != '\0'; --_len, ++_addr)		      \
	  {								      \
	    _bad = __stp_store_deref_nocheck(1, _addr, *_s++);		      \
	    if (_bad)					      		      \
	      break;							      \
	  }								      \
	if (!_bad)							      \
	  __stp_store_deref_nocheck(1, _addr, '\0');			      \
      }									      \
    pagefault_enable();                                                       \
    set_fs(_oldfs);                                                           \
    if (_bad)								      \
      STORE_DEREF_FAULT(addr);						      \
  })


/* 
 * store_kderef_string(): safely write a string to kernel memory
 *
 * src: source string
 * addr: address to write to
 * maxbytes: maximum number of bytes to write
 * 
 * The macro has no return value. If this macro gets an error while
 * trying to write, a STORE_DEREF_FAULT will occur. Note that the
 * kernel will not pagefault when trying to write the memory.
 */

#define store_kderef_string(src, addr, maxbytes)			      \
  _stp_store_deref_string((src), (addr), (maxbytes), KERNEL_DS)


/* 
 * store_uderef_string(): safely write a string to user memory
 *
 * src: source string
 * addr: address to write to
 * maxbytes: maximum number of bytes to write
 * 
 * The macro has no return value. If this macro gets an error while
 * trying to write, a STORE_DEREF_FAULT will occur. Note that the
 * kernel will not pagefault when trying to write the memory.
 */

#define store_uderef_string(src, addr, maxbytes)			      \
  _stp_store_deref_string((src), (addr), (maxbytes), USER_DS)

#endif /* _LINUX_LOC2C_RUNTIME_H_ */
