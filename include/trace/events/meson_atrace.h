/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

/*
 * This file enables ftrace logging in a way that
 * atrace/systrace would understand
 * without any custom javascript change in chromium-trace
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM meson_atrace

#if !defined(_TRACE_MESON_BASE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MESON_BASE_H

#include <linux/tracepoint.h>

#define KERNEL_ATRACE_COUNTER 0
#define KERNEL_ATRACE_BEGIN 1
#define KERNEL_ATRACE_END 2
#define KERNEL_ATRACE_ASYNC_BEGIN 3
#define KERNEL_ATRACE_ASYNC_END 4

#if !defined(TRACE_HEADER_MULTI_READ)

enum {
	KERNEL_ATRACE_TAG_VIDEO,
	KERNEL_ATRACE_TAG_CODEC_MM,
	KERNEL_ATRACE_TAG_VDEC,
	KERNEL_ATRACE_TAG_TSYNC,
	KERNEL_ATRACE_TAG_IONVIDEO,
	KERNEL_ATRACE_TAG_AMLVIDEO,
	KERNEL_ATRACE_TAG_VIDEO_COMPOSER,
	KERNEL_ATRACE_TAG_V4L2,
	KERNEL_ATRACE_TAG_CPUFREQ,
	KERNEL_ATRACE_TAG_MSYNC,
	KERNEL_ATRACE_TAG_DEMUX,
	KERNEL_ATRACE_TAG_MEDIA_SYNC,
	KERNEL_ATRACE_TAG_DDR_BW,
	KERNEL_ATRACE_TAG_DIM,
	KERNEL_ATRACE_TAG_MAX = 64,
	KERNEL_ATRACE_TAG_ALL
};

#endif

#define print_flags_header(flags) __print_flags(flags, "",	\
	{ (1UL << KERNEL_ATRACE_COUNTER),		"C" },	\
	{ (1UL << KERNEL_ATRACE_BEGIN),			"B" },	\
	{ (1UL << KERNEL_ATRACE_END),			"E" },	\
	{ (1UL << KERNEL_ATRACE_ASYNC_BEGIN),		"S" },	\
	{ (1UL << KERNEL_ATRACE_ASYNC_END),		"F" })

#define print_flags_delim(flags) __print_flags(flags, "",	\
	{ (1UL << KERNEL_ATRACE_COUNTER),		"|1|" },\
	{ (1UL << KERNEL_ATRACE_BEGIN),			"|1|" },\
	{ (1UL << KERNEL_ATRACE_END),			"" },	\
	{ (1UL << KERNEL_ATRACE_ASYNC_BEGIN),		"|1|" },\
	{ (1UL << KERNEL_ATRACE_ASYNC_END),		"|1|" })

TRACE_EVENT(tracing_mark_write,
	    TP_PROTO(const char *name, unsigned int flags, unsigned long value),
	    TP_ARGS(name, flags, value),

	TP_STRUCT__entry(__string(name, name)
			 __field(unsigned int, flags)
			 __field(unsigned long, value)
	),

	TP_fast_assign(__assign_str(name, name);
		       __entry->flags = flags;
		       __entry->value = value;
	),

	TP_printk("%s%s%s|%lu", print_flags_header(__entry->flags),
		  print_flags_delim(__entry->flags),
		  __get_str(name), __entry->value)
);

#ifdef CONFIG_AMLOGIC_DEBUG_ATRACE
void meson_atrace(int tag, const char *name, unsigned int flags,
		  unsigned long value);

#define ATRACE_COUNTER(name, value) \
	meson_atrace(KERNEL_ATRACE_TAG, name, \
		(1 << KERNEL_ATRACE_COUNTER), value)
#define ATRACE_BEGIN(name) \
	meson_atrace(KERNEL_ATRACE_TAG, name, \
		(1 << KERNEL_ATRACE_BEGIN), 0)
#define ATRACE_END() \
	meson_atrace(KERNEL_ATRACE_TAG, "", \
		(1 << KERNEL_ATRACE_END), 1)
#define ATRACE_ASYNC_BEGIN(name, cookie) \
	meson_atrace(KERNEL_ATRACE_TAG, name, \
		(1 << KERNEL_ATRACE_ASYNC_BEGIN), cookie)
#define ATRACE_ASYNC_END(name, cookie) \
	meson_atrace(KERNEL_ATRACE_TAG, name, \
		(1 << KERNEL_ATRACE_ASYNC_END), cookie)
#else
static inline void meson_atrace(int tag, const char *name, unsigned int flags,
		  unsigned long value)
{
}

#define ATRACE_COUNTER(name, value)
#define ATRACE_BEGIN(name)
#define ATRACE_END(name)
#define ATRACE_ASYNC_BEGIN(name, cookie)
#define ATRACE_ASYNC_END(name, cookie)
#endif

#endif /* _TRACE_MESON_BASE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
