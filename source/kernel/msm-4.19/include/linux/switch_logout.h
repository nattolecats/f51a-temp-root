/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SWITCH_LOGOUT__
#define __SWITCH_LOGOUT__

#include <linux/seq_file.h>

/* SWITCH_LOGOUT=Invalidate */
/* Not operate custom_pringk. */
#define SWITCH_LOGOUT
#define CALLBACK_TIME_WARNING_REPORT
/* #define POWER_ON_BENCH_LOG */

#define WARNING_REPORT_TIME			(HZ / 10)	/* 100ms */

/* Support 32 kinds of up to log. */
#define GpioLogType					0x00000001	/* GPIO Dump Log.               */

#define ClockLogType				0x00000080	/* Clock Log.                   */

#define Resume_LogType				0x00000100	/* Resume(Callback) Log.        */
#define Resume_Early_LogType		0x00000200	/* Resume Early(Callback) Log.  */
#define Resume_Noirq_LogType		0x00000400	/* Resume Noirq(Callback) Log.  */
#define RegulatorLogType 			0x00000800	/* Regulator Log.               */

#define BeforeSuspend_LogType		0x00010000	/* Before Suspend Log.          */
#define AfterSuspend_LogType		0x00020000	/* After Suspend Log.           */
#define BeforeInitDriver_LogType	0x00040000	/* Before driver_init Log.      */

#define GPIOLIB_SWITCH_DUMP_SIZE 16384
#define REGULATOR_SWITCH_DUMP_SIZE 65536
#define CLK_SWITCH_DUMP_SIZE 65536

#ifdef SWITCH_LOGOUT
	/* To support 32 types, please to change the type to long. */
	uint32_t check_and_get_table_from_NV(void);
	void switch_print_seq_kmsg(struct seq_file *s);
	void *switch_print_seq_alloc(unsigned long size);
	void switch_print_seq_free(struct seq_file *s);
	void switch_gpio_dump(unsigned int);
	void switch_gpio_dump_op(void);
	void switch_powercollapse_dump(void);
	void switch_before_suspend_gpio_dump(void);
	void switch_after_suspend_gpio_dump(void);
	void switch_before_init_driver_gpio_dump(void);
#else /* SWITCH_LOGOUT */
	static inline uint32_t check_and_get_table_from_NV(void) {return 0;}
	static void switch_print_seq_kmsg(struct seq_file *s) {};
	static void *switch_print_seq_alloc(unsigned long size) {return NULL;}
	static void switch_print_seq_free(struct seq_file *s) {}
	static inline void switch_gpio_dump(unsigned int detail) {}
	static inline void switch_gpio_dump_op(void) {};
	static inline void switch_powercollapse_dump(void) {};
	static inline void switch_before_suspend_gpio_dump(void) {};
	static inline void switch_after_suspend_gpio_dump(void) {};
	static inline void switch_before_init_driver_gpio_dump(void) {};
#endif /* SWITCH_LOGOUT */

#define gpio_NV_Swich_dump(detail) \
			switch_gpio_dump(detail)

/* Power on log output by changing POWER_ON_BENCH_LOG. */
#if defined (SWITCH_LOGOUT) && defined (POWER_ON_BENCH_LOG)
	#define poweron_bench_printk(fmt, ...) \
				printk("[BenchLog(PW_ON)]: %s(%d) " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
	#define poweron_bench_printk(fmt, ...) \
				do { } while(0)
#endif /* SWITCH_LOGOUT && POWER_ON_BENCH_LOG */

/* Log change by changing NV Value. */
#define resume_NV_switch_printk(fmt, ...)							\
({																	\
if (check_and_get_table_from_NV() & Resume_LogType)					\
printk("[Resume]:%s(%d) " fmt, __func__, __LINE__, ##__VA_ARGS__);	\
})

#define resume_early_NV_switch_printk(fmt, ...)								\
({																			\
if (check_and_get_table_from_NV() & Resume_Early_LogType)					\
printk("[Resume Early]:%s(%d) " fmt, __func__, __LINE__, ##__VA_ARGS__);	\
})

#define resume_noirq_NV_switch_printk(fmt, ...)								\
({																			\
if (check_and_get_table_from_NV() & Resume_Noirq_LogType)					\
printk("[Resume Noirq]:%s(%d) " fmt, __func__, __LINE__, ##__VA_ARGS__);	\
})


#endif /* __SWITCH_LOGOUT__ */
