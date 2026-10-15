/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Touch Driver Atrace Tracing Integration
 *
 * Copyright (C) 2023 Google, Inc.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM systrace
#if !defined(_TRACE_TOUCH_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TOUCH_TRACE_H
#include <linux/tracepoint.h>

/* make declarations singleton at recursive inclusion path */
#if !defined(_TOUCH_TRACE_RECURSIVE_DECLARATIONS)
#define _TOUCH_TRACE_RECURSIVE_DECLARATIONS
enum {
	TOUCH_TRACE_EVENT_BEGIN = (1UL << 0),
	TOUCH_TRACE_EVENT_END = (1UL << 1),
	TOUCH_TRACE_EVENT_INT64 = (1UL << 2),
};
#endif /* _TOUCH_TRACE_RECURSIVE_DECLARATIONS */
#define touch_trace_event_type(flag)             \
	__print_flags(flag, "",               \
		{ TOUCH_TRACE_EVENT_BEGIN, "B" },\
		{ TOUCH_TRACE_EVENT_END, "E" },  \
		{ TOUCH_TRACE_EVENT_INT64, "C" })

/*
 * Report touch coordinate with the format: [BEC|pid|input_x|input_y]
 *
 */
TRACE_EVENT(touch_input_pos,
	TP_PROTO(int flag, int pid, const char *func_name, int64_t x_value, int64_t y_value),
	TP_ARGS(flag, pid, func_name, x_value, y_value),
	TP_STRUCT__entry(
		__field(int, flag)
		__field(int, pid)
		__string(func_name, func_name)
		__field(int64_t, x_value)
		__field(int64_t, y_value)
	),
	TP_fast_assign(
		__entry->flag = flag;
		__entry->pid = pid;
		__assign_str(func_name, func_name);
		__entry->x_value = x_value;
		__entry->y_value = y_value;
	),
	TP_printk("%s|%d|%s: input_x=%lld, input_y=%lld",
		touch_trace_event_type(__entry->flag),
		__entry->pid,
		__get_str(func_name),
		__entry->x_value,
		__entry->y_value
	)
);

/*
 * Report the duration of code section with the format: [BE|pid|name]
 * The duration of systrace:0 can be visualized with Perfetto.
 *
 */
TRACE_EVENT(0,
	TP_PROTO(int flag, int pid, const char *name),
	TP_ARGS(flag, pid, name),
	TP_STRUCT__entry(
		__field(int, flag)
		__field(int, pid)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->flag = flag;
		__entry->pid = pid;
		__assign_str(name, name);
	),
	TP_printk("%s|%d|%s",
		touch_trace_event_type(__entry->flag),
		__entry->pid,
		__get_str(name)
	)
);

/*
 * Declare touch event class, print with the format: [C|pid|name|value]
 *
 */
DECLARE_EVENT_CLASS(touch_event_template,
	TP_PROTO(int flag, int pid, const char *name, int64_t value),
	TP_ARGS(flag, pid, name, value),
	TP_STRUCT__entry(
		__field(int, flag)
		__field(int, pid)
		__string(name, name)
		__field(int64_t, value)
	),
	TP_fast_assign(
		__entry->flag = flag;
		__entry->pid = pid;
		__assign_str(name, name);
		__entry->value = value;
	),
	TP_printk("%s|%d|%s|%lld",
		touch_trace_event_type(__entry->flag),
		__entry->pid,
		__get_str(name),
		__entry->value
	)
);

DEFINE_EVENT(touch_event_template, touch_wakeup_gesture,
	TP_PROTO(int flag, int pid, const char *name, int64_t value),
	TP_ARGS(flag, pid, name, value)
);

DEFINE_EVENT(touch_event_template, touch_palm_gesture,
	TP_PROTO(int flag, int pid, const char *name, int64_t value),
	TP_ARGS(flag, pid, name, value)
);

DEFINE_EVENT(touch_event_template, touch_event,
	TP_PROTO(int flag, int pid, const char *name, int64_t value),
	TP_ARGS(flag, pid, name, value)
);

#define __ATRACE_POS_PID(pid, func_name, x_value, y_value) \
	trace_touch_input_pos(\
		TOUCH_TRACE_EVENT_INT64, \
		pid, \
		func_name,\
		x_value, \
		y_value)

#define ATRACE_TOUCH_POS(func_name, x_value, y_value) \
	__ATRACE_POS_PID(current->tgid, func_name, x_value, y_value)

#define __ATRACE_INT_PID(pid, name, value) \
	trace_touch_event(\
		TOUCH_TRACE_EVENT_INT64,\
		pid,			\
		name,			\
		value)

#define ATRACE_INT(name, value)		\
	__ATRACE_INT_PID(current->tgid, name, value)

#define ATRACE_EVENT(event_name)		\
	__ATRACE_INT_PID(current->tgid, event_name, 0)

#define ATRACE_WAKEUP() \
	trace_touch_wakeup_gesture(\
		TOUCH_TRACE_EVENT_INT64,\
		current->tgid,\
		"",\
		0)

#define ATRACE_PALM() \
	trace_touch_palm_gesture(\
		TOUCH_TRACE_EVENT_INT64,\
		current->tgid,\
		"",\
		0)

#define __ATRACE_BEGIN_PID(pid, name)	\
	trace_0(\
		TOUCH_TRACE_EVENT_BEGIN,\
		pid,			\
		name)

#define __ATRACE_END_PID(pid, name)	\
	trace_0(\
		TOUCH_TRACE_EVENT_END,\
		pid,			\
		name)

#define ATRACE_BEGIN(name)		\
	__ATRACE_BEGIN_PID(current->tgid, name)

#define ATRACE_END()			\
	__ATRACE_END_PID(current->tgid, "")

#define ATRACE_BLOCK(name, block)	\
do {					\
	ATRACE_BEGIN(name);		\
	(block);			\
	ATRACE_END();			\
} while (0)


#endif /* _TRACE_SYSTRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../google-modules/touch/common
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE touch_trace
#include <trace/define_trace.h>
