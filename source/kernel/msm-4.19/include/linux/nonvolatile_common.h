/*
 * (C) 2021 FCNT LIMITED
 */
/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _NONVOLATILE_COMMON_H
#define _NONVOLATILE_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <linux/ioctl.h>

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif


/*------------------------------------------------------------------*/

/*------------------------------------------------------------------*/
enum nv_command_result
{
	nv_success,
	nv_not_initialized,
	nv_not_active,
	nv_invalid_id,
	nv_invalid_buffer,
	nv_invalid_size,
	nv_invalid_access,
	nv_failed,
	nv_encrypted,
	nv_out_of_memory,
	// 
	nv_command_result_count
};


enum nv_mc_clear_mode
{
	nv_mc_clear_master,
	nv_mc_clear_type_1,
	nv_mc_clear_type_2,
	// 
	nv_mc_clear_mode_count
};


typedef struct nonvolatile_data
{
	uint8_t*		iBuff;
	unsigned long	iId;
	unsigned long	iSize;
	int				iResult;
	unsigned long	iEncrypted;
} nonvolatile_data_t;


#define IOC_MAGIC 'w'

#define IOCTL_SET_NONVOLATILE	_IOW(IOC_MAGIC, 1, nonvolatile_data_t)
#define IOCTL_GET_NONVOLATILE	_IOR(IOC_MAGIC, 2, nonvolatile_data_t)
#define IOCTL_CLEAR_NONVOLATILE	_IOR(IOC_MAGIC, 3, enum nv_mc_clear_mode)

#ifdef __KERNEL__
int get_nonvolatile(uint8_t *, unsigned long, unsigned long);
int set_nonvolatile(uint8_t *, unsigned long, unsigned long);
int clear_nonvolatile(enum nv_mc_clear_mode);
int set_boot_nonvoltaile(loff_t offset_from_bootarea_begin, const uint8_t *buf, size_t len);
bool check_nonvolatile_init(void);
#endif

#ifdef CONFIG_COMPAT
typedef struct compat_nonvolatile_data
{
	compat_uptr_t	iBuff;
	compat_ulong_t	iId;
	compat_ulong_t	iSize;
	compat_int_t	iResult;
	compat_ulong_t	iEncrypted;
} compat_nonvolatile_data_t;

#define COMPAT_IOCTL_SET_NONVOLATILE		_IOW(IOC_MAGIC, 1, compat_nonvolatile_data_t)
#define COMPAT_IOCTL_GET_NONVOLATILE		_IOR(IOC_MAGIC, 2, compat_nonvolatile_data_t)
#define COMPAT_IOCTL_CLEAR_NONVOLATILE		_IOR(IOC_MAGIC, 3, enum nv_mc_clear_mode)
#endif /* CONFIG_COMPAT */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _NONVOLATILE_COMMON_H */

