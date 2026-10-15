/* (C) 2021 FCNT LIMITED */
/*
 * FJSEC LSM module
 *
 * based on deckard.c
 * based on root_plug.c
 * Copyright (C) 2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 * _xx_encode(), _xx_realpath_from_path()
 * is ported from security/tomoyo/realpath.c in linux-2.6.32
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 *
 * _xx_encode(), _xx_get_absolute_path, _xx_get_dentry_path, 
 * _xx_get_local_path, _xx_get_socket_name, _xx_realpath_from_path()
 * are ported from security/tomoyo/realpath.c in linux-3.4.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/mnt_namespace.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/magic.h>
#include <asm/mman.h>
#include <linux/msdos_fs.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <asm/memory.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <asm/page.h>
#include <linux/securebits.h>
#include <linux/capability.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/audit.h>
#include <linux/memblock.h>
#include <linux/fs.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <net/sock.h>
#include <linux/dcache.h>
#include <linux/time.h>
#include <linux/lsm_hooks.h>

#include "fjsec_config.h"



//#define dprintk printk
#define dprintk(...)

static enum boot_mode_type boot_mode = BOOT_MODE_NONE;
static bool mount_flag = true;

#define LSM_NV_CHECK_ON
#ifdef LSM_NV_CHECK_ON
static char lsm_enable_flag = FJSEC_LSM_ENABLED;
#endif

static char lsm_nv_flag = FJSEC_LSM_NV_BEFORE;


#if !defined(DOCOMO_RELEASE) || !defined(LSM_FLG_OFF)
#ifdef LSM_NV_CHECK_ON
#define __fjsec_check_nv(a) (lsm_enable_flag & 0x0F & a)
#else
#define __fjsec_check_nv(a) (0UL)
#endif
#else
#define __fjsec_check_nv(a) (1UL)
#endif /* !defined(DOCOMO_RELEASE) || !defined(LSM_FLG_OFF) */

#define  FJLSM_REJECT(ret)\
	if(__fjsec_check_nv(FJSEC_LSM_PERMISSIVE) == FJSEC_LSM_PERMISSIVE) return 0; \
	return ret;

//#define ROOTFS_RW_REMOUNT

static int __init bootmode_falg_param(char *str)
{
	if (strcmp(str, BOOT_ARGS_MODE_REPAIR) == 0) {
		lsm_enable_flag = FJSEC_LSM_DISABLED;
	}
	return 1;
}

__setup("androidboot.mode=", bootmode_falg_param);

#ifdef VTS_TEST
static char vtslock_flag[8];

static int __init vtslock_flag_param(char *line)
{
	strlcpy(vtslock_flag, line, sizeof(vtslock_flag));
	return 1;
}

__setup("androidboot.vtslockoff=", vtslock_flag_param);

static inline bool is_unlocked(void)
{
	static const char vtslockoff[] = "true";

	return !strncmp(vtslock_flag, vtslockoff, sizeof(vtslockoff));
}
#endif  /* VTS_TEST */

static int fjsec_check_page_acl_ext(void);



char *_xx_encode(const char *str)
{
	int len = 0;
	const char *p = str;
	char *cp;
	char *cp0;

	if (!p) {
	    printk(KERN_INFO "%s:%d: LSM_ERROR_1 arguement str is null.\n", __FUNCTION__, __LINE__);
		return NULL;
	}

	while (*p) {
		const unsigned char c = *p++;
		if (c == '\\')
			len += 2;
		else if (c > ' ' && c < 127)
			len++;
		else
			len += 4;
	}
	len++;
	/* Reserve space for appending "/". */
	cp = kzalloc(len + 10, GFP_NOFS);
	if (!cp) {
	    printk(KERN_INFO "%s:%d: LSM_ERROR_2 cannot allocate memory.\n", __FUNCTION__, __LINE__);
		return NULL;
	}

	cp0 = cp;
	p = str;
	while (*p) {
		const unsigned char c = *p++;

		if (c == '\\') {
			*cp++ = '\\';
			*cp++ = '\\';
		} else if (c > ' ' && c < 127) {
			*cp++ = c;
		} else {
			*cp++ = '\\';
			*cp++ = (c >> 6) + '0';
			*cp++ = ((c >> 3) & 7) + '0';
			*cp++ = (c & 7) + '0';
		}
	}
	return cp0;
}

/**
 * _xx_get_absolute_path - Get the path of a dentry but ignores chroot'ed root.
 *
 * @path:   Pointer to "struct path".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns the buffer on success, an error code otherwise.
 *
 * If dentry is a directory, trailing '/' is appended.
 */
static char* _xx_get_absolute_path(const struct path *path, char * const buffer,
				      const int buflen)
{
	char *pos = ERR_PTR(-ENOMEM);
	if (buflen >= 256) {
		/* go to whatever namespace root we are under */
		pos = d_absolute_path(path, buffer, buflen - 1);
		if (!IS_ERR(pos) && *pos == '/' && pos[1]) {
			struct inode *inode = path->dentry->d_inode;
			if (inode && S_ISDIR(inode->i_mode)) {
				buffer[buflen - 2] = '/';
				buffer[buflen - 1] = '\0';
			}
		}
	}
	return pos;
}

/**
 * _xx_get_dentry_path - Get the path of a dentry.
 *
 * @dentry: Pointer to "struct dentry".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns the buffer on success, an error code otherwise.
 *
 * If dentry is a directory, trailing '/' is appended.
 */
static char* _xx_get_dentry_path(struct dentry *dentry, char * const buffer,
				    const int buflen)
{
	char *pos = ERR_PTR(-ENOMEM);
	if (buflen >= 256) {
		pos = dentry_path_raw(dentry, buffer, buflen - 1);
		if (!IS_ERR(pos) && *pos == '/' && pos[1]) {
			struct inode *inode = dentry->d_inode;
			if (inode && S_ISDIR(inode->i_mode)) {
				buffer[buflen - 2] = '/';
				buffer[buflen - 1] = '\0';
			}
		}
	}
	return pos;
}

/**
 * _xx_get_local_path - Get the path of a dentry.
 *
 * @dentry: Pointer to "struct dentry".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns the buffer on success, an error code otherwise.
 */
static char* _xx_get_local_path(struct dentry *dentry, char * const buffer,
				   const int buflen)
{
	struct super_block *sb = dentry->d_sb;
	char *pos = _xx_get_dentry_path(dentry, buffer, buflen);
	if (IS_ERR(pos))
		return pos;
	/* Convert from $PID to self if $PID is current thread. */
	if (sb->s_magic == PROC_SUPER_MAGIC && *pos == '/') {
		char *ep;
		const pid_t pid = (pid_t) simple_strtoul(pos + 1, &ep, 10);
		if (*ep == '/' && pid && pid ==
		    task_tgid_nr_ns(current, sb->s_fs_info)) {
			pos = ep - 5;
			if (pos < buffer)
				goto out;
			memmove(pos, "/self", 5);
		}
		goto prepend_filesystem_name;
	}
	/* Use filesystem name for unnamed devices. */
	if (!MAJOR(sb->s_dev))
		goto prepend_filesystem_name;
	{
		struct inode *inode = sb->s_root->d_inode;
		/*
		 * Use filesystem name if filesystem does not support rename()
		 * operation.
		 */
		if (!inode->i_op->rename)
			goto prepend_filesystem_name;
	}
	/* Check device name and prepend path name. */
	{
		char name[64];
		int name_len;
		const dev_t dev = sb->s_dev;
		struct fs_path_config *pc;
		struct fs_path_config *work_devs;

		work_devs = devs;
		dprintk(KERN_INFO "%s:%d: lsm_nv_flag = 0x%02x\n", __FUNCTION__, __LINE__, lsm_nv_flag);

		for (pc = work_devs; pc->prefix; pc++) {
			if (dev == pc->rdev) {
			    dprintk(KERN_INFO "%s:%d: dev is found. dev(%u,%u)\n", __FUNCTION__, __LINE__, MAJOR(dev), MINOR(dev));
				if( pc->mnt_pnt ) {
					name_len = strlen(pc->mnt_pnt);
					pos -= name_len;
					if (pos < buffer)
						goto out;
					memmove(pos, pc->mnt_pnt, name_len);
				} else {
					name[sizeof(name) - 1] = '\0';
					snprintf(name, sizeof(name) - 1, "dev(%u,%u):", MAJOR(dev), MINOR(dev));
					name_len = strlen(name);
					pos -= name_len;
					if (pos < buffer)
						goto out;
					memmove(pos, name, name_len);
				}
				break;
			}
		}
		return pos;
	}
	/* Prepend filesystem name. */
prepend_filesystem_name:
	{
		const char *name = sb->s_type->name;
		const int name_len = strlen(name);
		pos -= name_len + 1;
		if (pos < buffer)
			goto out;
		memmove(pos, name, name_len);
		pos[name_len] = ':';
	}
	return pos;
out:
	return ERR_PTR(-ENOMEM);
}

/**
 * _xx_get_socket_name - Get the name of a socket.
 *
 * @path:   Pointer to "struct path".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns the buffer.
 */
static char* _xx_get_socket_name(const struct path *path, char * const buffer,
				    const int buflen)
{
	struct inode *inode = path->dentry->d_inode;
	struct socket *sock = inode ? SOCKET_I(inode) : NULL;
	struct sock *sk = sock ? sock->sk : NULL;
	if (sk) {
		snprintf(buffer, buflen, "socket:[family=%u:type=%u:"
			 "protocol=%u]", sk->sk_family, sk->sk_type,
			 sk->sk_protocol);
	} else {
		snprintf(buffer, buflen, "socket:[unknown]");
	}
	return buffer;
}

/**
 * _xx_realpath_from_path - Returns realpath(3) of the given pathname but ignores chroot'ed root.
 *
 * @path: Pointer to "struct path".
 *
 * Returns the realpath of the given @path on success, NULL otherwise.
 *
 * If dentry is a directory, trailing '/' is appended.
 * Characters out of 0x20 < c < 0x7F range are converted to
 * \ooo style octal string.
 * Character \ is converted to \\ string.
 *
 * These functions use kzalloc(), so the caller must call kfree()
 * if these functions didn't return NULL.
 */
// temporarly fix, add retry count handling and exit it surely.
// some log output are added to it.
#define MAX_RETRY_COUNT 3
static char* _xx_realpath_from_path_2(const struct path *path)
{
	char *buf = NULL;
	char *name = NULL;
	unsigned int buf_len = PAGE_SIZE / 2;
	struct dentry *dentry = path->dentry;
	struct super_block *sb;
	unsigned int retry_count = 0;

	if (!dentry) {
	    printk(KERN_INFO "%s:%d: LSM_ERROR_1 dentry null.\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	sb = dentry->d_sb;
	while (1) {
		char *pos;
		struct inode *inode;
		buf_len <<= 1;
		kfree(buf);
		buf = kmalloc(buf_len, GFP_NOFS);
		if (!buf) {
		    printk(KERN_INFO "%s:%d: LSM_ERROR_2 cannot allocate memory. \n", __FUNCTION__, __LINE__);
			break;
		}
		/* To make sure that pos is '\0' terminated. */
		buf[buf_len - 1] = '\0';
		/* Get better name for socket. */
		if (sb->s_magic == SOCKFS_MAGIC) {
			pos = _xx_get_socket_name(path, buf, buf_len - 1);
			goto encode;
		}
		/* For "pipe:[\$]". */
		if (dentry->d_op && dentry->d_op->d_dname) {
			pos = dentry->d_op->d_dname(dentry, buf, buf_len - 1);
			goto encode;
		}
		inode = sb->s_root->d_inode;
		/*
		 * Get local name for filesystems without rename() operation
		 * or dentry without vfsmount.
		 */
//		if (!path->mnt || (inode->i_op && !inode->i_op->rename))
//			pos = _xx_get_local_path(path->dentry, buf,
//						    buf_len - 1);
		/* Get absolute name for the rest. */
//		else {
			pos = _xx_get_absolute_path(path, buf, buf_len - 1);
			/*
			 * Fall back to local name if absolute name is not
			 * available.
			 */
			if (pos == ERR_PTR(-EINVAL)) {
//				printk(KERN_INFO "_xx_get_absolute_path failed. Maybe lazy unmount race issue happens. <%s>\n", __func__);
//				printk(KERN_INFO "_xx_get_absolute_path. res = %s. <%s>\n", buf, __func__);
				pos = _xx_get_local_path(path->dentry, buf, buf_len - 1);
			    printk(KERN_INFO "%s:%d: _xx_get_local_path. res = %s\n", __FUNCTION__, __LINE__, pos);
			}
//		}
encode:
		if (IS_ERR(pos)) {
		    printk(KERN_INFO "%s:%d: path null. try again. err = %lx  filename is %s \n", __FUNCTION__, __LINE__, PTR_ERR(pos), path->dentry->d_name.name );

			if (++retry_count > MAX_RETRY_COUNT) {
				kfree(buf);
			    printk(KERN_ERR "%s:%d: Unable to get info.\n", __FUNCTION__, __LINE__ );
				return NULL;
			}
			continue;
		}
		name = _xx_encode(pos);
		break;
	}
	kfree(buf);
	if (!name) {
	    dprintk(KERN_INFO "%s:%d: out of memory happens.\n", __FUNCTION__, __LINE__ );
	}

	return name;
}

static int _xx_realpath_from_path(const struct path *path, char *newname, int newname_len)
{
	char *str;

	str = _xx_realpath_from_path_2(path);
	if(!str) {
	    printk(KERN_INFO "%s:%d: LSM_ERROR_1 newname_len=%u \n", __FUNCTION__, __LINE__, newname_len );
		return -1;
	} else {
		memset(newname, '\0', newname_len);
		if( strlen(str) < newname_len ) strncpy(newname, str, strlen(str));
//		newname[newname_len-1] = '\0';
		kfree(str);
		if( strlen(newname) == 0 ) {
    	    printk(KERN_INFO "%s:%d: LSM_ERROR_2 newname_len=%d\n", __FUNCTION__, __LINE__, newname_len);
			return -1; 
		}
	}

	return 0;
}

static int __init setup_mode(char *str)
{
	if (strcmp(str, BOOT_ARGS_MODE_FOTA) == 0) {
		boot_mode = BOOT_MODE_FOTA;
	} else if (strcmp(str, BOOT_ARGS_MODE_MASTERCLEAR) == 0) {
		boot_mode = BOOT_MODE_MASTERCLEAR;
	} else if (strcmp(str, BOOT_ARGS_MODE_OSUPDATE) == 0) {
		boot_mode = BOOT_MODE_OSUPDATE;
	} else if (strcmp(str, BOOT_ARGS_MODE_RECOVERYMENU) == 0) {
		boot_mode = BOOT_MODE_SDDOWNLOADER;
	} else if (strcmp(str, BOOT_ARGS_MODE_CHARGER) == 0) {
		boot_mode = BOOT_MODE_CHARGER;
	}

	dprintk(KERN_INFO "boot mode=<%d>\n", boot_mode);
	return 0;
}
early_param("androidboot.mode", setup_mode);

static char *get_process_path(struct task_struct *task, char *buf, size_t size)
{
	struct mm_struct *mm;
	char *cp = NULL;

	mm = task->mm;
	if (!mm) {
		dprintk(KERN_INFO "%s:%d mm is null.\n", __FUNCTION__, __LINE__);
		return NULL;
	}

	down_read(&mm->mmap_sem);
	if (mm->exe_file) {
		cp = d_path(&mm->exe_file->f_path, buf, size);
	}
	up_read(&mm->mmap_sem);

	return cp;
}

#ifdef DUMP_PRINFO
static void dump_prinfo(const char *function_name, int line_number)
{
	char *binname;
	char *buf = kzalloc(PATH_MAX, GFP_NOFS);

	if (!buf) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return;
	}
	binname = get_process_path(current, buf, PATH_MAX-1);
	printk(KERN_INFO "%s:%d: current process name=<%s>, path=<%s>, uid=<%d>\n"
			, function_name, line_number, current->comm, binname, CURRENT_UID);
	kfree(buf);
}

static void dump_prinfo_mapping(const char *function_name, int line_number)
{
	char *binname;
	char *buf = kzalloc(PATH_MAX, GFP_NOFS);

	if (!buf) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return;
	}
	binname = get_process_path(current, buf, PATH_MAX-1);
	printk(KERN_INFO "%s:%d: current process name=<%s>, path=<%s>, uid=<%d>\n"
			, function_name, line_number, current->comm, binname, CURRENT_UID);
	kfree(buf);
}
#else /* DUMP_PRINFO */
#define dump_prinfo(a, b)
#define dump_prinfo_mapping(a, b)
#endif /* DUMP_PRINFO */


static int fjsec_check_access_process_path(char *process_path)
{
	char *buf = kzalloc(PATH_MAX, GFP_NOFS);
	char *binname;

	if (!buf) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	binname = get_process_path(current, buf, PATH_MAX-1);
	if (binname == NULL || IS_ERR(binname)) {
		printk(KERN_INFO "%s:%d: Failed getting process path. process name=%s, uid=%d, pid=%d\n"
			   , __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		kfree(buf);
		return -EPERM;
	}

	dprintk(KERN_INFO "%s:%d: process path=%s\n", __FUNCTION__, __LINE__, binname);

	if (strcmp(binname, process_path) == 0) {
		kfree(buf);
		return 0;
	}

	dprintk(KERN_INFO "%s:%d: mismatched process path. config=<%s>, current=<%s>, process name=%s, uid=%d\n"
			, __FUNCTION__, __LINE__, process_path, binname, current->comm, CURRENT_UID);

	kfree(buf);
	return -EPERM;
}

static int fjsec_check_access_parent_process_path_mapping(struct task_struct *task, char *process_path)
{
	char *buf = kzalloc(PATH_MAX, GFP_NOFS);
	char *binname;

	if (!buf) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s\n",
			 __FUNCTION__, __LINE__, task->comm);
		return -ENOMEM;
	}

	binname = get_process_path(task, buf, PATH_MAX-1);
	if (binname == NULL || IS_ERR(binname)) {
		printk(KERN_INFO "%s:%d: Failed getting process path. process name=%s\n"
			   , __FUNCTION__, __LINE__, task->comm);
		kfree(buf);
		return -EPERM;
	}

	if (strcmp(binname, process_path) == 0) {
		kfree(buf);
		return 0;
	}

	dprintk(KERN_INFO "%s:%d: process name=%s ,process path=%s\n", __FUNCTION__, __LINE__, task->comm, binname);

	kfree(buf);
	return -EPERM;
}

static int getprocname(char *buf, size_t buflen) {
	int res = -1;
	unsigned int len;
	char* buffer;

	struct mm_struct *mm = current->mm;

	if (!mm)
		goto out;

 	len = mm->arg_end - mm->arg_start;
 
	if (len > PAGE_SIZE)
		len = PAGE_SIZE;

	buffer = kmalloc(len, GFP_NOFS);
	if( !buffer ) goto out;
	
	res = access_remote_vm(mm, mm->arg_start, buffer, len, 0);

	// If the nul at the end of args has been overwritten, then
	// assume application is using setproctitle(3).
	if (res > 0 && buffer[res-1] != '\0' && len < PAGE_SIZE) {
		len = strnlen(buffer, res);
		if (len < res) {
		    res = len;
		} else {
			len = mm->env_end - mm->env_start;
			if (len > PAGE_SIZE - res)
				len = PAGE_SIZE - res;
			res += access_remote_vm(mm, mm->env_start, buffer+res, len, 0);
			res = strnlen(buffer, res);
		}
	}

	if( res > buflen ) res = buflen -1;
	memset(buf, '\0', buflen);
	memcpy(buf, buffer, res);

	kfree(buffer);
	
out:
	return res;
}

#define MAX_PROC_NAME_BUFLEN		256
static int fjsec_check_access_process(char *process_name, char *process_path)
{
	char procnamebuf[MAX_PROC_NAME_BUFLEN] ={0};
	int process_name_len = 0;
	int ret;
	char *strtoken;

  if(!process_name || !process_path)
    return -EPERM;

	ret = getprocname(procnamebuf, MAX_PROC_NAME_BUFLEN);
	dprintk(KERN_INFO "%s:%d: s_procname=%s, d_procname=%s(%d)\n", __FUNCTION__, __LINE__, process_name, procnamebuf, ret);

	if( ret == -1 ) {
		process_name_len = strlen(process_name);
		if ( process_name_len > (TASK_COMM_LEN - 1)) {
			if( memcmp(current->comm, process_name, TASK_COMM_LEN-1) != 0 ) {
				process_name += (process_name_len - (TASK_COMM_LEN - 1));
				printk(KERN_INFO "%s:%d: procname is adjusted.(%s)\n", __FUNCTION__, __LINE__, process_name);
			} else return fjsec_check_access_process_path(process_path);
		}
	} else {
		if( (strtoken = strrchr(procnamebuf, '/')) != NULL ) {
			process_name_len = strlen(strtoken);
			memmove(procnamebuf, strtoken+1, process_name_len);
			procnamebuf[process_name_len]='\0';
		}
	}
	if(( memcmp(procnamebuf, process_name, strlen(process_name)) == 0 ) ||
	   ( memcmp(current->comm, process_name, strlen(process_name)) == 0 )) {
		if (fjsec_check_access_process_path(process_path) == 0) {
			return 0;
		} else {
			dprintk(KERN_INFO "%s:%d: mismatched process path. config=<%s>\n"
				, __FUNCTION__, __LINE__, process_path);
		}
	} else {
		dprintk(KERN_INFO "%s:%d: mismatched process name. config=<%s>, current=<%s>\n"
				, __FUNCTION__, __LINE__, process_name, procnamebuf);
	}
	return -EPERM;
}




#ifdef CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE
static int fjsec_check_secure_storage_access_process(void)
{

#if 0
	if (boot_mode == BOOT_MODE_FOTA) {
		if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_FOTA_MODE_ACCESS_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_FOTA_MODE_ACCESS_PROCESS_PATH) == 0) {
			return 0;
		}
	}
	else 
#endif
	if (boot_mode == BOOT_MODE_SDDOWNLOADER) {
		if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_PATH) == 0) {
			return 0;
		}
	}
	else if (boot_mode == BOOT_MODE_OSUPDATE) {
		if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_OSUPDATE_MODE_ACCESS_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_OSUPDATE_MODE_ACCESS_PROCESS_PATH) == 0) {
			return 0;
		}
	}
//	else if (boot_mode == BOOT_MODE_RECOVERY) {
	else if (boot_mode == BOOT_MODE_MASTERCLEAR) {
		if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_RECOVERY_MODE_ACCESS_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_RECOVERY_MODE_ACCESS_PROCESS_PATH) == 0) {
			return 0;
		}
	}

	if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_SECURE_STORAGE_ACCESS_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_SECURE_STORAGE_ACCESS_PROCESS_PATH) == 0) {
			return 0;
	}

	if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_KEYMASTER_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_KEYMASTER_PROCESS_PATH) == 0) {
			return 0;
	}

	if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_KMINSTALL_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_KMINSTALL_PROCESS_PATH) == 0) {
			return 0;
	}

	if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_FSCK_PROCESS_NAME,
										CONFIG_SECURITY_FJSEC_FSCK_PROCESS_PATH) == 0) {
			return 0;
	}

	if (fjsec_check_access_process(INIT_PROCESS_NAME, INIT_PROCESS_PATH) == 0) {
		dprintk(KERN_INFO "%s:%d:init proc, boot mode=<%d>\n", __FUNCTION__, __LINE__, boot_mode);
		return 0;
	}

	return -1;
}

static int fjsec_check_secure_storage_directory_access_process(char *realpath)
{

	if (strncmp(realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_DIR_PATH,
					strlen(CONFIG_SECURITY_FJSEC_SECURE_STORAGE_DIR_PATH)) == 0) {
		if (fjsec_check_secure_storage_access_process() == 0) {
			return 0;
		}

		return -EPERM;
	}

	return 0;
}

static int fjsec_check_secure_storage_device_access_process(char *realpath)
{

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_DEV_PATH) == 0) {
		if (fjsec_check_secure_storage_access_process () == 0) {
			return 0;
		}

		return -EPERM;
	}

	return 0;
}

#else /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */

#define fjsec_check_secure_storage_directory_access_process(a) (0)
#define fjsec_check_secure_storage_device_access_process(a) (0)

#endif /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */

#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING

static int fjsec_check_kitting_access_process(char *process_name, char *process_path)
{

    dprintk(KERN_INFO "%s:%d: process name=%s\n", __FUNCTION__, __LINE__, current->comm);

	if (strcmp(current->group_leader->comm, process_name) == 0) {
		if (fjsec_check_access_process_path(process_path) == 0) {
			return 0;
		}
	} else {
		dprintk(KERN_INFO "%s:%d: mismatched process name. config=<%s>, current=<%s>\n"
				, __FUNCTION__, __LINE__, process_name, current->group_leader->comm);
	}

	return -EPERM;
}

static int fjsec_check_kitting_directory_access_process(char *realpath)
{

	if (strncmp(realpath, CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH,
					strlen(CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH)) == 0) {
		if (boot_mode == BOOT_MODE_NONE) {
			struct kitting_directory_access_process *kd_access_control_process;
			
			for (kd_access_control_process = kitting_directory_access_process_list; kd_access_control_process->process_path; kd_access_control_process++) {
				char *process_name;
				int process_name_len;

				if (kd_access_control_process->uid != UID_NO_CHECK && kd_access_control_process->uid != CURRENT_UID) {
					dprintk(KERN_INFO "%s:%d: mismatched process UID\n", __FUNCTION__, __LINE__);
					continue;
				}

				process_name_len = strlen(kd_access_control_process->process_name);
				process_name = kd_access_control_process->process_name;

				if (process_name_len > (TASK_COMM_LEN - 1)) {
					process_name += (process_name_len - (TASK_COMM_LEN - 1));
				}

				if (fjsec_check_kitting_access_process(process_name,
												kd_access_control_process->process_path) != 0) {
					dprintk(KERN_INFO "%s:%d: mismatched process name, process path\n", __FUNCTION__, __LINE__);
					continue;
				}

				dprintk(KERN_INFO "%s:%d: SUCCESS realpath=%s\n", __FUNCTION__, __LINE__, realpath);
				return 0;
			}
		}

		if (fjsec_check_access_process(INIT_PROCESS_NAME, INIT_PROCESS_PATH) == 0) {
			dprintk(KERN_INFO "%s:%d:init proc, boot mode=<%d>\n", __FUNCTION__, __LINE__, boot_mode);
			return 0;
		}

        return -EPERM;
	}

	return 0;
}

static int fjsec_check_kitting_device_access_process(char *realpath)
{

    if (strcmp(realpath, CONFIG_SECURITY_FJSEC_KITTING_DEV_PATH) == 0) {
		if (boot_mode == BOOT_MODE_SDDOWNLOADER) {
			if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_NAME,
											CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_PATH) == 0) {
				return 0;
			}
			if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_MK2FS_NAME,
											CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_MK2FS_PATH) == 0) {
				return 0;
			}
		} 
//		else if (boot_mode == BOOT_MODE_RECOVERY) {
		else if (boot_mode == BOOT_MODE_MASTERCLEAR) {
			if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_RECOVERY_MODE_ACCESS_PROCESS_NAME,
											CONFIG_SECURITY_FJSEC_RECOVERY_MODE_ACCESS_PROCESS_PATH) == 0) {
				return 0;
			}
			if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_MK2FS_NAME,
											CONFIG_SECURITY_FJSEC_SDDOWNLOADER_MODE_ACCESS_PROCESS_MK2FS_PATH) == 0) {
				return 0;
			}
		}

		return -EPERM;
	}

	return 0;
}

#else /* CONFIG_SECURITY_FJSEC_AC_KITTING */

#define fjsec_check_kitting_directory_access_process(a) (0)
#define fjsec_check_kitting_device_access_process(a) (0)

#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */

	
static int fjsec_read_write_access_control(char *realpath)
{
	struct read_write_access_control *rw_access_control;
	struct read_write_access_control_process *rw_access_control_process;
	int result = 1;

//	if (!(boot_mode == BOOT_MODE_NONE || boot_mode == BOOT_MODE_RECOVERY)) {
	if (!(boot_mode == BOOT_MODE_NONE || boot_mode == BOOT_MODE_MASTERCLEAR)) {
		return 1;
	}
#ifdef VTS_TEST
	if (is_unlocked()) {
		return 1;
	}
#endif  /* VTS_TEST */

	for (rw_access_control = rw_access_control_list; rw_access_control->prefix; rw_access_control++) {
		int length = strlen(rw_access_control->prefix);

		if (rw_access_control->prefix[length - 1] == '*') {
			if (strncmp(rw_access_control->prefix, realpath, length - 1) != 0) {
				continue;
			}
		} else {
			if (strcmp(rw_access_control->prefix, realpath) != 0) {
				continue;
			}
		}

        if ( (CURRENT_UID == AID_ROOT || CURRENT_UID == AID_SYSTEM) && (rw_access_control->f_strict == NO_STRICT) ) {
				return 0;
		}
		
		result = -EPERM;

		for (rw_access_control_process = rw_access_control->process_list; rw_access_control_process->process_path; rw_access_control_process++) {
			char *process_name;
			int process_name_len;

			dprintk (KERN_INFO "  config=<%s><%s><%s><%d>\n"
					, rw_access_control->prefix, rw_access_control_process->process_name, rw_access_control_process->process_path, rw_access_control_process->uid);
			if (rw_access_control_process->uid != UID_NO_CHECK && rw_access_control_process->uid != CURRENT_UID) {
				dprintk(KERN_INFO "%s:%d: mismatched process UID\n", __FUNCTION__, __LINE__);
				continue;
			}

			if (rw_access_control_process->process_name != PRNAME_NO_CHECK) {
				process_name_len = strlen(rw_access_control_process->process_name);
				process_name = rw_access_control_process->process_name;

//				if (process_name_len > (TASK_COMM_LEN - 1)) {
//					process_name += (process_name_len - (TASK_COMM_LEN - 1));
//				}

				if (fjsec_check_access_process(process_name,
												rw_access_control_process->process_path) != 0) {
					dprintk(KERN_INFO "%s:%d: mismatched process name, process path\n", __FUNCTION__, __LINE__);
					continue;
				}
			} else {
				if (fjsec_check_access_process_path(rw_access_control_process->process_path) != 0) {
					dprintk(KERN_INFO "%s:%d: mismatched process path\n", __FUNCTION__, __LINE__);
					continue;
				}
			}

			dprintk(KERN_INFO "%s:%d: SUCCESS realpath=%s\n", __FUNCTION__, __LINE__, realpath);
			return 0;
		}
	}
	return result;
}
#ifdef CONFIG_SECURITY_PATH
#if 0
static bool fjsec_check_mknod_system_root(char *realpath, const char *devpath){
	if((current->pid == INIT_PID) &&
		(CURRENT_UID == AID_ROOT) &&
		(strcmp(realpath, SYSROOT_DEV_PATH) == 0) &&
		(strcmp(devpath, CONFIG_SECURITY_FJSEC_SYSTEM_DEV_PATH) == 0)){
		return true;
	}
	return false;
}
#endif

static bool fjsec_check_chroot_system_root(char *realpath){
	if((current->pid == INIT_PID) &&
		(CURRENT_UID == AID_ROOT) &&
		(strcmp(realpath, ROOT_DIR) == 0)){
		return true;
	}
	return false;
}

static void fjsec_print_path(const struct path *path)
{
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer.\n", __FUNCTION__, __LINE__);
		return;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return;
	}
	printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s.\n", __FUNCTION__, __LINE__, realpath);
	kfree(realpath);
}

static int fjsec_check_path_from_path(const struct path *path)
{
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return r;
	}

	r = fjsec_read_write_access_control(realpath);
	if (r == -EPERM) {
		dump_prinfo(__FUNCTION__, __LINE__);
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s, process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -EPERM;
	}
	else if (r == 0) {
		kfree(realpath);
		return 0;
	}

	if (fjsec_check_secure_storage_directory_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
		__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -EPERM;
	}

	if (fjsec_check_kitting_directory_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
		__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -EPERM;
	}

	kfree(realpath);
	return 0;
}

static int fjsec_check_chmod_from_path(const struct path *path, mode_t mode)
{
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;
	struct fs_path_config *pc;
	struct fs_path_config *work_devs;

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return r;
	}

	dprintk(KERN_INFO "(%s:%d) path=<%s> mode=<%d>\n", __FUNCTION__, __LINE__, realpath, mode);

	work_devs = devs;
	dprintk(KERN_INFO "%s:%d: lsm_nv_flag = 0x%02x\n", __FUNCTION__, __LINE__, lsm_nv_flag);

	for (pc = work_devs; pc->prefix; pc++) {
		if (strcmp(realpath, pc->prefix) == 0) {
			if (mode == (pc->mode & S_IALLUGO)) {
				break;
			}

			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
	    		 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			return -EPERM;
		}
	}

	kfree(realpath);
	return 0;
}

static int fjsec_check_chmod_from_mode(const struct path *path, mode_t mode)
{
	struct inode *inode = path->dentry->d_inode;
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;

	if((boot_mode == BOOT_MODE_FOTA) || (boot_mode == BOOT_MODE_SDDOWNLOADER)){
		if(realpath) kfree(realpath);
		return 0;
	}

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer.\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if ((strncmp(realpath, "/data/data/", strlen("/data/data")) == 0) ||
	     (strncmp(realpath, "/data/user/", strlen("/data/user/")) == 0) ||
	     (strncmp(realpath, "/data/user_de/", strlen("/data/user_de/")) == 0) ||
	     (strncmp(realpath, "/data/media/", strlen("/data/media/")) == 0) ||
	     (strncmp(realpath, "/storage/emulated/", strlen("/storage/emulated/")) == 0) ||
	     (strncmp(realpath, "/mnt/expand/", strlen("/mnt/expand/")) == 0)) {
		kfree(realpath);
		return 0;
	}
#ifdef VTS_TEST
	if ((is_unlocked()) && (strncmp(realpath, "/data/local/", strlen("/data/local")) == 0)) {
		kfree(realpath);
		return 0;
	}
#endif  /* VTS_TEST */

	if((inode->i_uid.val == 0) && !(inode->i_mode & S_ISUID)) {
		if (mode  & S_ISUID) {
			fjsec_print_path(path);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT process name=%s file name=%s current mode=0%o \n",
				   __FUNCTION__, __LINE__, current->comm, path->dentry->d_iname,inode->i_mode);
			kfree(realpath);
			return -EPERM;
		}
	}

	if((inode->i_gid.val == 0) && !(inode->i_mode & S_ISGID)) {
		if (mode  & S_ISGID) {
			fjsec_print_path(path);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT process name=%s file name=%s current mode=0%o \n",
				   __FUNCTION__, __LINE__, current->comm, path->dentry->d_iname,inode->i_mode);
			kfree(realpath);
			return -EPERM;
		}
	}

	kfree(realpath);
	return 0;
}

static int fjsec_check_chown_from_path(const struct path *path, kuid_t uid, kgid_t gid)
{
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;
	struct fs_path_config *pc;
	struct fs_path_config *work_devs;

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return r;
	}

	dprintk(KERN_INFO "(%s:%d) <%s>\n", __FUNCTION__, __LINE__, realpath);

	work_devs = devs;
	dprintk(KERN_INFO "%s:%d: lsm_nv_flag = 0x%02x\n", __FUNCTION__, __LINE__, lsm_nv_flag);

	for (pc = work_devs; pc->prefix; pc++) {
		if (strcmp(realpath, pc->prefix) == 0) {
			if (((uid.val == pc->uid) || (uid.val == -1)) && ((gid.val == pc->gid) || (gid.val == -1))) {
				break;
			}
			if (((strcmp(realpath, CONFIG_SECURITY_FJSEC_FRP_DEV_PATH) == 0)) && (uid.val == AID_ROOT) && (gid.val == AID_ROOT)) {
				break;
			}
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
		    	 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			return -EPERM;
		}
	}

	kfree(realpath);
	return 0;
}

static int fjsec_check_chown_from_id(const struct path *path, kuid_t uid, kgid_t gid)
{
    struct inode *inode = path->dentry->d_inode;
	char *realpath;
	int r;

	if((boot_mode == BOOT_MODE_FOTA) || (boot_mode == BOOT_MODE_SDDOWNLOADER)){
		return 0;
	}
	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer.\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}
	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return r;
	}

	if ((strncmp(realpath, "/storage/emulated/", strlen("/storage/emulated/")) == 0)) {
		kfree(realpath);
		return 0;
	}

	kfree(realpath);

	if((inode->i_mode & S_ISUID) && (inode->i_uid.val != 0)) {
		if (uid.val == 0) {
			fjsec_print_path(path);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT process name=%s file name=%s current uid=%d \n",
				   __FUNCTION__, __LINE__, current->comm, path->dentry->d_iname, inode->i_uid.val);
			return -EPERM;
		}
	}

	if((inode->i_mode & S_ISGID) && (inode->i_gid.val != 0)) {
		if (gid.val == 0) {
			fjsec_print_path(path);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT process name=%s file name=%s current gid=%d \n",
				   __FUNCTION__, __LINE__, current->comm, path->dentry->d_iname, inode->i_gid.val);
			return -EPERM;
		}
	}

	return 0;
}

static int fjsec_check_mknod(const struct path *path, struct dentry *dentry,
			     int mode, unsigned int dev)
{
	char *realpath = kzalloc(PATH_MAX, GFP_NOFS);
	int r;
	struct fs_path_config *pc;
	struct fs_path_config *work_devs;
	u32 rdev;

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		return r;
	}

	if (PATH_MAX > strlen(realpath) + strlen(dentry->d_name.name)) {
		strcat(realpath, dentry->d_name.name);
	} else {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -ENOMEM;
	}
    

	dprintk(KERN_INFO "(%s:%d) <%s> <%s>\n", __FUNCTION__, __LINE__, realpath, dentry->d_name.name);

//	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_SYMLINK_PATH) == 0 ||
//		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_SYMLINK_PATH) == 0) {
//		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
//			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
//		kfree(realpath);
//		return -EPERM;
//	}

	rdev = new_encode_dev(dev);

	work_devs = devs;
	dprintk(KERN_INFO "%s:%d: lsm_nv_flag = 0x%02x\n", __FUNCTION__, __LINE__, lsm_nv_flag);
#if 0
	for (pc = work_devs; pc->prefix; pc++) {
		if (rdev != pc->rdev) {
			continue;
		}

		if (mode != pc->mode) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			     __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			return -EPERM;
		}

		if (strcmp(realpath, pc->prefix) != 0) {
			if(!fjsec_check_mknod_system_root(realpath, pc->prefix)){
				printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
					 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
				kfree(realpath);
				return -EPERM;
			}
		}

		// rdev, mode, realpath all matched
		kfree(realpath);
		return 0;
	}
#endif
	for (pc = work_devs; pc->prefix; pc++) {
		if (strcmp(realpath, pc->prefix) != 0) {
			continue;
		}

		if (mode != pc->mode) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
    			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			return -EPERM;
		}
#if 0
		if (rdev != pc->rdev) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
	    		 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			return -EPERM;
		}
#endif
		// realpath, mode, rdev all matched
		kfree(realpath);
		return 0;
	}

	kfree(realpath);
	return 0;
}
#endif
static int fjsec_check_mount_point(char *realpath, char *dev_realpath)
{
#ifndef CONFIG_DM_VERITY
	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH) == 0) {
		if ((strncmp(dev_realpath, CONFIG_SECURITY_FJSEC_DM_DEV_PATH, strlen(CONFIG_SECURITY_FJSEC_DM_DEV_PATH))) == 0) {
			return MATCH_SYSTEM_MOUNT_POINT;
		}
#ifdef CONFIG_SECURITY_FJSEC_SYSTEM_AS_ROOT
		else if (strcmp(dev_realpath, "") == 0) {
			return MATCH_SYSTEM_MOUNT_POINT;
		}
#endif
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH) == 0) {
		if ((strncmp(dev_realpath, CONFIG_SECURITY_FJSEC_DM_DEV_PATH, strlen(CONFIG_SECURITY_FJSEC_DM_DEV_PATH))) == 0) {
			return MATCH_VENDOR_MOUNT_POINT;
		}
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}
#endif // CONFIG_DM_VERITY

#ifdef CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE
	if (strcmp(dev_realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_DEV_PATH) == 0) {
		if (strcmp(realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_MOUNT_POINT_PATH) == 0) {
			return MATCH_SECURE_STORAGE_MOUNT_POINT;
		}
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}
	else if (strncmp(realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_MOUNT_POINT_PATH,
						strlen(CONFIG_SECURITY_FJSEC_SECURE_STORAGE_MOUNT_POINT_PATH)) == 0) {
		return MISMATCH_MOUNT_POINT;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */

#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING
	if (strcmp(dev_realpath, CONFIG_SECURITY_FJSEC_KITTING_DEV_PATH) == 0) {
		if (strcmp(realpath, CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH) == 0) {
			return MATCH_KITTING_MOUNT_POINT;
		}
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}
	else if (strncmp(realpath, CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH,
						strlen(CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH)) == 0) {
		return MISMATCH_MOUNT_POINT;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */
#ifdef CONFIG_SECURITY_FJSEC_AC_FIRMWARE
	if (strcmp(dev_realpath, CONFIG_SECURITY_FJSEC_FIRMWARE_DEV_PATH) == 0) {
		if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FIRMWARE_DIR_PATH) == 0) {
			return MATCH_MOUNT_POINT;
		}
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}
	else if (strncmp(realpath, CONFIG_SECURITY_FJSEC_FIRMWARE_DIR_PATH,
						strlen(CONFIG_SECURITY_FJSEC_FIRMWARE_DIR_PATH)) == 0) {
		return MISMATCH_MOUNT_POINT;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_FIRMWARE */
#ifdef CONFIG_SECURITY_FJSEC_AC_DSP
	if (strcmp(dev_realpath, CONFIG_SECURITY_FJSEC_DSP_DEV_PATH) == 0) {
		if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DSP_DIR_PATH) == 0) {
			return MATCH_MOUNT_POINT;
		}
		else {
			return MISMATCH_MOUNT_POINT;
		}
	}
	else if (strncmp(realpath, CONFIG_SECURITY_FJSEC_DSP_DIR_PATH,
						strlen(CONFIG_SECURITY_FJSEC_DSP_DIR_PATH)) == 0) {
		return MISMATCH_MOUNT_POINT;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_DSP */

	if (strcmp(realpath, ROOT_DIR) == 0) {
		return MATCH_ROOT_MOUNT_POINT;
	}
#ifdef DEBUG_ACCESS_CONTROL
        if (strcmp(dev_realpath, TEST_PTN_PATH) == 0) {
                if (strcmp(realpath, TEST_MOUNT_POINT) == 0) {
                        return MATCH_MOUNT_POINT;
                }
                else {
                        return MISMATCH_MOUNT_POINT;
                }
        }
        else if (strncmp(realpath, TEST_MOUNT_POINT, strlen(TEST_MOUNT_POINT)) == 0) {
                return MISMATCH_MOUNT_POINT;
        }
#endif /* DEBUG_ACCESS_CONTROL */

	return UNRELATED_MOUNT_POINT;
}



static int fjsec_sb_mount(const char *dev_name, const struct path *path,
			   const char *type, unsigned long flags, void *data)
{
	char *realpath;
	char *dev_realpath;
	int r;
	enum result_mount_point result;
	struct path dev_path;
	struct fs_path_config *pc;
	struct fs_path_config *work_devs;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

#ifdef VTS_TEST
	if (is_unlocked()) {
		return 0;
	}
#endif  /* VTS_TEST */

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	dev_realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!dev_realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -ENOMEM;
	}
	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		kfree(dev_realpath);
		FJLSM_REJECT(r)
	}

	dprintk(KERN_INFO "(%s:%d) <%s>\n", __FUNCTION__, __LINE__, realpath);
	if(NULL == dev_name){
		printk(KERN_INFO "(%s:%d) <%s>\n", __FUNCTION__, __LINE__, realpath);
		if ((strcmp(current->comm, INIT_PROCESS_NAME) == 0) || (CURRENT_UID == AID_ROOT)) {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;
		}
		else if ((strcmp(realpath, "/storage/") == 0) || (strcmp(realpath, "/") == 0) || (strcmp(realpath, "/apex/") == 0)) {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;
		}
		else{
			printk(KERN_INFO "%s:%d: FJLSM_REJECT dev_name is null but realpath is not /storage/. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
			 kfree(realpath);
			 kfree(dev_realpath);
			 FJLSM_REJECT(r)
		}
	}
	r = kern_path(dev_name, LOOKUP_FOLLOW, &dev_path);
	if (r == 0) {
		r = _xx_realpath_from_path(&dev_path, dev_realpath, PATH_MAX-1);
		path_put(&dev_path);
		if (r != 0) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
			kfree(realpath);
			kfree(dev_realpath);
			FJLSM_REJECT(r)
		}
	}

	dprintk(KERN_INFO "(%s:%d) <%s> <%s>\n", __FUNCTION__, __LINE__, dev_name, dev_realpath);

	result = fjsec_check_mount_point(realpath, dev_realpath);

	dprintk(KERN_INFO "(%s:%d) mount point check result=<%d>\n", __FUNCTION__, __LINE__, result);
	if ((result == MATCH_SYSTEM_MOUNT_POINT) || (result == MATCH_VENDOR_MOUNT_POINT)) {
		if (boot_mode == BOOT_MODE_FOTA || boot_mode == BOOT_MODE_SDDOWNLOADER || boot_mode == BOOT_MODE_OSUPDATE || boot_mode == BOOT_MODE_MASTERCLEAR || boot_mode == BOOT_MODE_CHARGER ) {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;
		}

		if ((flags & MS_RDONLY) == 0) {
			if ((flags & MS_REMOUNT) == 0) {
				if (mount_flag == false) {
					mount_flag = true;
				}
				else {
					printk(KERN_INFO "%s:%d: FJLSM_REJECT R/W MOUNT dev_realpath=%s realpath=%s process name=%s, uid=%d, pid=%d\n",
						__FUNCTION__, __LINE__, dev_realpath, realpath, current->comm, CURRENT_UID, current->pid);
					kfree(realpath);
					kfree(dev_realpath);
					FJLSM_REJECT(-EPERM)
				}
			}
			else {
				printk(KERN_INFO "%s:%d: FJLSM_REJECT R/W REMOUNT dev_realpath=%s realpath=%s process name=%s, uid=%d, pid=%d\n",
					__FUNCTION__, __LINE__, dev_realpath, realpath, current->comm, CURRENT_UID, current->pid);
				kfree(realpath);
				kfree(dev_realpath);
				FJLSM_REJECT(-EPERM)
			}
		}

		kfree(realpath);
		kfree(dev_realpath);
		return 0;
	}
#ifdef CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE
	else if (result == MATCH_SECURE_STORAGE_MOUNT_POINT) {
		kfree(realpath);
		kfree(dev_realpath);
		return 0;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */
#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING
	else if (result == MATCH_KITTING_MOUNT_POINT) {
		kfree(realpath);
		kfree(dev_realpath);
		return 0;
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */
	else if (result == MATCH_MOUNT_POINT || result == UNRELATED_MOUNT_POINT) {
		kfree(realpath);
		kfree(dev_realpath);
		return 0;
	}
	else if (result == MATCH_ROOT_MOUNT_POINT) {
#ifdef ROOTFS_RW_REMOUNT
		printk(KERN_INFO "%s:%d: rootfs is allowed to remount with rw flag for CT.",__FUNCTION__, __LINE__);
		kfree(realpath);
		kfree(dev_realpath);
		return 0;
#endif /* ROOTFS_RW_REMOUNT */
		if (flags & MS_RDONLY) {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;
		}		

		if (flags & MS_REMOUNT) {		
			printk(KERN_INFO "%s:%d: FJLSM_REJECT R/W REMOUNT dev_realpath=%s realpath=%s process name=%s, uid=%d, pid=%d\n",
						__FUNCTION__, __LINE__, dev_realpath, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			kfree(dev_realpath);
			FJLSM_REJECT(-EPERM)
		}else {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;

		}
	}
	work_devs = devs;

	for (pc = work_devs; pc->prefix; pc++) {
		if (pc->mnt_pnt && !strncmp(pc->prefix, dev_realpath, strlen(pc->prefix)) && !strncmp(pc->mnt_pnt, realpath, strlen(pc->mnt_pnt))) {
			kfree(realpath);
			kfree(dev_realpath);
			return 0;
		}
	}

    printk(KERN_INFO "%s:%d: FJLSM_REJECT MOUNT dev_realpath=%s realpath=%s process name=%s, uid=%d, pid=%d\n",
		__FUNCTION__, __LINE__, dev_realpath, realpath, current->comm, CURRENT_UID, current->pid);
	kfree(realpath);
	kfree(dev_realpath);
	FJLSM_REJECT(-EPERM)
}

static int fjsec_sb_umount(struct vfsmount *mnt, int flags)
{
	char *realpath;
	struct path path = { mnt, mnt->mnt_root };
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(&path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	dprintk(KERN_INFO "%s:%d:(%s).\n", __FUNCTION__, __LINE__, realpath);
	if ((strcmp(realpath, CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH) == 0) ||
	    (strcmp(realpath, CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH) == 0)){
		if (boot_mode == BOOT_MODE_FOTA) {
			if (fjsec_check_access_process(CONFIG_SECURITY_FJSEC_FOTA_MODE_ACCESS_PROCESS_NAME,
										   CONFIG_SECURITY_FJSEC_FOTA_MODE_ACCESS_PROCESS_PATH) == 0) {
				kfree(realpath);
				return 0;
			}
			if (fjsec_check_access_process(SECURITY_FJSEC_RECOVERY_PROCESS_NAME,
										   SECURITY_FJSEC_RECOVERY_PROCESS_PATH) == 0) {
				kfree(realpath);
				return 0;
			}
		}
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}
#ifdef CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE
	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_SECURE_STORAGE_MOUNT_POINT_PATH) == 0) {
		if (fjsec_check_access_process( INIT_PROCESS_NAME, INIT_PROCESS_PATH) == 0 ) {
			kfree(realpath);
			return 0;
		}
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_SECURE_STORAGE */
#ifdef CONFIG_SECURITY_FJSEC_AC_KITTING
	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_KITTING_DIR_PATH) == 0) {
        if (boot_mode == BOOT_MODE_MASTERCLEAR || boot_mode == BOOT_MODE_SDDOWNLOADER ) {

            if (fjsec_check_access_process(SECURITY_FJSEC_RECOVERY_PROCESS_NAME,
										   SECURITY_FJSEC_RECOVERY_PROCESS_PATH) == 0) {
				kfree(realpath);
				return 0;
			}

        }
		if (fjsec_check_access_process( INIT_PROCESS_NAME, INIT_PROCESS_PATH) == 0 ) {
			kfree(realpath);
			return 0;
		}
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}
#endif /* CONFIG_SECURITY_FJSEC_AC_KITTING */

	kfree(realpath);
	return 0;
}

static int fjsec_sb_pivotroot(const struct path *old_path, const struct path *new_path)
{
	char *old_realpath;
	char *new_realpath;
	int r;

#ifdef VTS_TEST
	if (is_unlocked()) {
		return 0;
	}
#endif  /* VTS_TEST */

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	old_realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!old_realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	new_realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!new_realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		kfree(old_realpath);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(old_path, old_realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, old_path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(old_realpath);
		kfree(new_realpath);
		FJLSM_REJECT(r)
	}

	r = _xx_realpath_from_path(new_path, new_realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, new_path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(old_realpath);
		kfree(new_realpath);
		FJLSM_REJECT(r)
	}

	printk(KERN_INFO "%s:%d: FJLSM_REJECT old_path=%s new_path=%s process name=%s, uid=%d, pid=%d\n",
		__FUNCTION__, __LINE__, old_realpath, new_realpath, current->comm, CURRENT_UID, current->pid);
	kfree(old_realpath);
	kfree(new_realpath);
	FJLSM_REJECT(-EPERM)
}

#ifdef CONFIG_SECURITY_FJSEC_PROTECT_CHROOT
static int fjsec_path_chroot(const struct path *path)
{
	char *realpath;
	char *tmp;
	char *p, *p2;
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	tmp = kzalloc(PATH_MAX, GFP_NOFS);
	if (!tmp) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		kfree(tmp);
		FJLSM_REJECT(r)
	}

#ifdef VTS_TEST
	if ((is_unlocked()) && (strncmp(realpath, "/data/local/", strlen("/data/local")) == 0)) {
		kfree(realpath);
		kfree(tmp);
		return 0;
	}
#endif  /* VTS_TEST */

	p = CONFIG_SECURITY_FJSEC_CHROOT_PATH;
	while (*p) {
		p2 = strchr(p, ':');
		if (p2) {
			strncpy(tmp, p, (p2 - p));
			tmp[p2 - p] = 0;
		}
		else {
			strcpy(tmp, p);
		}

		if (strcmp(tmp, realpath) == 0) {
			kfree(realpath);
			kfree(tmp);
			return 0;
		}

		if (p2) {
			p = p2 + 1;
		}
		else {
			p += strlen(p);
		}
	}
	if(fjsec_check_chroot_system_root(realpath)){
		kfree(realpath);
		kfree(tmp);
		return 0;
	}
	printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
	kfree(realpath);
	kfree(tmp);
	FJLSM_REJECT(-EPERM)
}
#endif	/* CONFIG_SECURITY_FJSEC_PROTECT_CHROOT */

static struct _prevInfo {
	int				pid;
	gid_t			i_gid;
	unsigned long	i_ino;
	loff_t			i_size;
} prevInfo = { -1, -1, 0, 0 };
//static unsigned long repeat_cnt = 0;
static int prev_ret             = -100000;
//static int debug_info           = 0;
static int skip_always          = 0;
//#define REPEAT_LOG_LIMIT        1000
#define CONTENTS_BOUNDARY       (500*1024*1024ULL) //500MB

static void set_prevInfo(const struct inode *d_inode) {

	if( d_inode ) {
		prevInfo.i_ino = d_inode->i_ino;
		prevInfo.i_size = i_size_read(d_inode);
		prevInfo.i_gid = d_inode->i_gid.val;
	}
	prevInfo.pid = current->pid;
}

static int fjsec_file_permission(struct file *file, int mask)
{
	int r;
	char *realpath = NULL;
	struct inode *d_inode = file->f_path.dentry->d_inode;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	if( (mask & MAY_WRITE) == 0 && prev_ret != -100000 &&
		prevInfo.i_size > CONTENTS_BOUNDARY &&
		current->pid == prevInfo.pid &&
		d_inode &&
		d_inode->i_ino == prevInfo.i_ino &&
		i_size_read(d_inode) == prevInfo.i_size &&
		d_inode->i_gid.val == prevInfo.i_gid ) {
		if(!skip_always) {
			dprintk(KERN_INFO "%s:%d: always skip. comm=%s, dname=%s, iino=%lu, off=%lld, size=%lld\n", __FUNCTION__, __LINE__, current->comm, file->f_path.dentry->d_name.name, d_inode->i_ino, file->f_pos, prevInfo.i_size);
			skip_always = 1;
		}
		FJLSM_REJECT(prev_ret)
	}

	skip_always = 0;
	realpath = kzalloc(PATH_MAX, GFP_NOFS);

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(&file->f_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, file->f_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	/* For read case */
	if ( (mask & MAY_WRITE) == 0 )
	{
		set_prevInfo(d_inode);

		r = fjsec_read_write_access_control(realpath);
		if (r == -EPERM) {
			if((file->f_mode & FMODE_WRITE) == 0 && (fjsec_check_page_acl_ext() == 0)){
				dprintk(KERN_INFO "%s:%d : fjsec_check_page_acl_ext success path=%s \n",__FUNCTION__, __LINE__, realpath);
				kfree(realpath);
				prev_ret = -EPERM;
				return 0;
			}
			dump_prinfo(__FUNCTION__, __LINE__);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			prev_ret = -EPERM;
			FJLSM_REJECT(-EPERM)
		}
		else if (r == 0) {
			kfree(realpath);
			prev_ret = 0;
			return 0;
		}

		if (fjsec_check_secure_storage_directory_access_process(realpath) != 0) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			prev_ret = -EPERM;
			FJLSM_REJECT(-EPERM)
		}

		if (fjsec_check_kitting_directory_access_process(realpath) != 0) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			prev_ret = -EPERM;
			FJLSM_REJECT(-EPERM)
		}

		/* read mode -> OK! */
		kfree(realpath);
		prev_ret = 0;
		return 0;
	}
	else /* For write case */
	{
		r = fjsec_read_write_access_control(realpath);
		if (r == -EPERM) {
			dump_prinfo(__FUNCTION__, __LINE__);
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			FJLSM_REJECT(-EPERM)
		}
		else if (r == 0) {
			kfree(realpath);
			return 0;
		}

		if (fjsec_check_secure_storage_directory_access_process(realpath) != 0) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			FJLSM_REJECT(-EPERM)
		}

		if (fjsec_check_kitting_directory_access_process(realpath) != 0) {
			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
				__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
			kfree(realpath);
			FJLSM_REJECT(-EPERM)
		}
	}
	kfree(realpath);
	return 0;
}

static int fjsec_file_open(struct file *file)
{
	char *realpath;
	int r;
	struct inode *d_inode = file->f_path.dentry->d_inode;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = _xx_realpath_from_path(&file->f_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, file->f_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	r = fjsec_read_write_access_control(realpath);
	if (r == -EPERM) {
		if((file->f_mode & FMODE_WRITE) == 0 && (fjsec_check_page_acl_ext() == 0)){
			dprintk(KERN_INFO "%s:%d : fjsec_check_page_acl_ext success path=%s \n",__FUNCTION__, __LINE__, realpath);
			kfree(realpath);
			return 0;
		}
		dump_prinfo(__FUNCTION__, __LINE__);
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s, process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}
	else if (r == 0) {
		if ((file->f_mode & FMODE_WRITE) == 0) {
			set_prevInfo(d_inode);
//			printk(KERN_INFO "%s:%d: SetprevInfo\n", __FUNCTION__, __LINE__);
		}
		kfree(realpath);
		return 0;
	}

	if (fjsec_check_secure_storage_directory_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_secure_storage_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_kitting_directory_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_kitting_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	/* read only mode -> OK! */
	if ((file->f_mode & FMODE_WRITE) == 0) {
		set_prevInfo(d_inode);
		dprintk(KERN_INFO "%s:%d: SetprevInfo comm=%s, dname=%s, uid=%d, pid=%d, iino=%lu, igid=%d, size=%llu\n", 
__FUNCTION__, __LINE__, current->comm, file->f_path.dentry->d_name.name, CURRENT_UID, current->pid, d_inode->i_ino, d_inode->i_gid.val, i_size_read(d_inode));
		kfree(realpath);
		return 0;
	}

	kfree(realpath);
	return 0;
}

#ifdef CONFIG_SECURITY_PATH
static int fjsec_path_mknod(const struct path *path, struct dentry *dentry, umode_t mode,
			    unsigned int dev)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

#ifdef VTS_TEST
	if (is_unlocked()) {
		return 0;
	}
#endif  /* VTS_TEST */

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	r = fjsec_check_mknod(path, dentry, mode, dev);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	return 0;
}

static int fjsec_path_mkdir(const struct path *path, struct dentry *dentry, umode_t mode)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	return 0;
}

static int fjsec_path_rmdir(const struct path *path, struct dentry *dentry)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	return 0;
}

static int fjsec_path_unlink(const struct path *path, struct dentry *dentry)
{
	char *realpath;
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	if (PATH_MAX > strlen(realpath) + strlen(dentry->d_name.name)) {
		strcat(realpath, dentry->d_name.name);
	} else {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			   __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-ENOMEM)
	}

    r = fjsec_read_write_access_control(realpath);
    if (r == -EPERM) {
        dump_prinfo(__FUNCTION__, __LINE__);
        printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s, process name=%s, uid=%d, pid=%d\n",
            __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
        kfree(realpath);
        FJLSM_REJECT(-EPERM)
    }
    else if (r == 0) {
        kfree(realpath);
        return 0;
    }

	dprintk(KERN_INFO "(%s:%d) <%s> <%s>\n", __FUNCTION__, __LINE__, realpath, dentry->d_name.name);

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV_PATH) == 0 || 
		strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV2_PATH) == 0 ||
		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_DEV_PATH) == 0 ) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	kfree(realpath);
	return 0;
}

static int fjsec_path_symlink(const struct path *path, struct dentry *dentry,
			      const char *old_name)
{
	char *realpath;
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	r = _xx_realpath_from_path(path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, path->dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	if (PATH_MAX > strlen(realpath) + strlen(dentry->d_name.name)) {
		strcat(realpath, dentry->d_name.name);
	} else {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			   __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-ENOMEM)
	}

	dprintk(KERN_INFO "(%s:%d) <%s> <%s> <%s>\n", __FUNCTION__, __LINE__, realpath, dentry->d_name.name, old_name);

//	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_SYMLINK_PATH) == 0 ||
//		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_SYMLINK_PATH) == 0 ) {
//		if (strcmp(old_name, CONFIG_SECURITY_FJSEC_FELICA_DEV_PATH) != 0) {
//			printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
//				 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
//			kfree(realpath);
//			return -EPERM;
//		}
//	}

	kfree(realpath);
	return 0;
}


static int fjesc_check_main_disk_access(char *realpath)
{
	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH) == 0) {
		return -EPERM;
	} else if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH_2) == 0) {
		return -EPERM;
	} else if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH_3) == 0) {
		return -EPERM;
	} else if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH_4) == 0) {
		return -EPERM;
	} else if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH_5) == 0) {
		return -EPERM;
	} else if (strcmp(realpath, CONFIG_SECURITY_FJSEC_DISK_DEV_PATH_6) == 0) {
		return -EPERM;
	}
	return 0;
}


static int fjsec_path_link(struct dentry *old_dentry, const struct path *new_dir,
			   struct dentry *new_dentry)
{
	int r;
	char *realpath;
	struct path old_path = {new_dir->mnt, old_dentry};
	struct path new_path = {new_dir->mnt, new_dentry};

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = fjsec_check_path_from_path(new_dir);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	r = _xx_realpath_from_path(&old_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, old_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	dprintk(KERN_INFO "(%s:%d) <%s>\n", __FUNCTION__, __LINE__, realpath);

	if (fjesc_check_main_disk_access(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_secure_storage_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_kitting_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	r = _xx_realpath_from_path(&new_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, new_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	dprintk(KERN_INFO "(%s:%d) <%s>\n", __FUNCTION__, __LINE__, realpath);

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV_PATH) == 0 ||
		strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV2_PATH) == 0 ||
		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_DEV_PATH) == 0 ) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	kfree(realpath);
	return 0;
}

static int fjsec_path_rename(const struct path *old_dir, struct dentry *old_dentry,
			     const struct path *new_dir, struct dentry *new_dentry)
{
	int r;
	char *realpath;
	struct path old_path = {old_dir->mnt, old_dentry};
	struct path new_path = {new_dir->mnt, new_dentry};

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);
	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}

	r = fjsec_check_path_from_path(new_dir);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	r = _xx_realpath_from_path(&old_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, old_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	dprintk(KERN_INFO "(%s:%d) <%s> <%s>\n", __FUNCTION__, __LINE__, realpath, old_dentry->d_name.name);

	if (fjesc_check_main_disk_access(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_secure_storage_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (fjsec_check_kitting_device_access_process(realpath) != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			__FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV_PATH) == 0 || 
		strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV2_PATH) == 0 ||
		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_DEV_PATH) == 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	r = _xx_realpath_from_path(&new_path, realpath, PATH_MAX-1);
	if (r != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, new_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(r)
	}

	if (strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV_PATH) == 0 || 
		strcmp(realpath, CONFIG_SECURITY_FJSEC_FELICA_DEV2_PATH) == 0 ||
		strcmp(realpath, CONFIG_SECURITY_FJSEC_NFC_DEV_PATH) == 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, realpath, current->comm, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(-EPERM)
	}

	kfree(realpath);
	return 0;
}

static int fjsec_path_truncate(const struct path *path)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	return 0;
}

static int fjsec_path_chmod(const struct path *path, umode_t mode)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	r = fjsec_check_chmod_from_path(path, mode);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	r = fjsec_check_chmod_from_mode(path, mode);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}
	return 0;
}

static int fjsec_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	int r;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	r = fjsec_check_path_from_path(path);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	r = fjsec_check_chown_from_path(path, uid, gid);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	r = fjsec_check_chown_from_id(path, uid, gid);
	if (r) {
		printk(KERN_INFO "%s:%d: r=%d\n", __FUNCTION__, __LINE__, r);
		FJLSM_REJECT(r)
	}

	return 0;
}
#endif	/* CONFIG_SECURITY_PATH */




static int fjsec_check_process_name(const char* cur_name, const char* acl_name)
{
	char* strano = NULL;
	unsigned int len = 0;
	unsigned int acl_len = 0;

	if( !cur_name || !acl_name ) return -EINVAL;

	acl_len = strlen(acl_name);
	strano = strrchr(acl_name, '*');
	len = strano? strano-acl_name : acl_len;

	if( !strncmp(cur_name, acl_name, len) ) {
		return 0;
	}
	return -1;
}


static int fjsec_check_page_acl_ext(void) {
	struct ac_config_page_ext *pc;
	bool find_module = false;

#ifdef CTS_TEST
    if(find_module != true){
	    dprintk(KERN_INFO "%s:%d: CTS app's parent process name=%s\n", __FUNCTION__, __LINE__, current->group_leader->comm);
	    for (pc = page_parent_acl_ext; strncmp(pc->process_name, "0", 1) != 0; pc++) {
		    if (strcmp(pc->process_name, NO_CHECK_STR) != 0 && fjsec_check_process_name(current->group_leader->comm, pc->process_name) != 0)
			    continue;

		    if (strcmp(pc->process_path, NO_CHECK_STR) != 0 && fjsec_check_access_parent_process_path_mapping(current->group_leader, pc->process_path))
			    continue;

		    if (pc->uid != UID_NO_CHECK && pc->uid != CURRENT_UID)
		    	continue;

		    find_module = true;
		    break;
	    }
    }
#endif
	dprintk(KERN_INFO "%s:%d: find_module=%d, path=%s\n", __FUNCTION__, __LINE__, find_module, pc->process_path);
	if (find_module) {
		if(pc->supl1 == SUPL_NO_HASHCHECK){
			return 0;
		}
	}

	return -EPERM;
}

static int fjsec_check_setid_access_process(void)
{
	char *buf = NULL;
	char *binname;
	struct ac_config_setid *config;

	if (CURRENT_UID != AID_ROOT) {
		return 0;
	}

#ifdef VTS_TEST
	if (is_unlocked()) {
		return 0;
	}
#endif  /* VTS_TEST */

	buf = kzalloc(PATH_MAX, GFP_NOFS);
	if (!buf) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer. process name=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		return -ENOMEM;
	}
        
	binname = get_process_path(current, buf, PATH_MAX-1);
	if (binname == NULL || IS_ERR(binname)) {
		printk(KERN_INFO "%s:%d: Failed getting process path. process name=%s, uid=%d, pid=%d\n"
			   , __FUNCTION__, __LINE__, current->comm, CURRENT_UID, current->pid);
		kfree(buf);
		return -EPERM;
	}

	dprintk(KERN_INFO "%s:%d: process path=%s\n", __FUNCTION__, __LINE__, binname);
	if ((strncmp(binname, CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH,
				strlen(CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH))== 0) ||
	      (strncmp(binname, CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH,
				strlen(CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH))== 0)) {
		kfree(buf);
		return 0;
	}

	for (config = setid_acl; config->process_name; config++) {
		if (strcmp(current->comm, config->process_name) == 0) {
			if (strcmp(binname, config->process_path) == 0) {
				kfree(buf);
				return 0;
			}
		}
	}
	printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s process name=%s\n",
		   __FUNCTION__, __LINE__, binname, current->comm);

	kfree(buf);
	return -EPERM;


}

static int fjsec_task_fix_setuid(struct cred *new, const struct cred *old,int flags)
{
	int ret;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	ret = cap_task_fix_setuid(new, old, flags);
	if(ret)
		return ret;
	ret = fjsec_check_setid_access_process();
	if (ret) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT flags %d\n", __FUNCTION__, __LINE__, flags);
	}
	FJLSM_REJECT(ret)
}


static int fjsec_task_fix_setgid(struct cred *new, const struct cred *old,int flags)
{
	int ret;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

	ret = fjsec_check_setid_access_process();
	if (ret) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT flags %d\n", __FUNCTION__, __LINE__, flags);
		FJLSM_REJECT(ret)
	}

	return 0;
}

static int fjsec_bprm_set_creds(struct linux_binprm *bprm)
{
	char *realpath = NULL;
	int ret;

	if(__fjsec_check_nv(FJSEC_LSM_ENABLED) == 0)
		return 0;

#ifdef VTS_TEST
	if (is_unlocked()) {
		return 0;
	}
#endif  /* VTS_TEST */

	ret = cap_bprm_set_creds(bprm);
	if (ret != 0)
		return ret;

	realpath = kzalloc(PATH_MAX, GFP_NOFS);

	if (!realpath) {
		printk(KERN_INFO "%s:%d: InternalError Failed allocating buffer.\n", __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	ret = _xx_realpath_from_path(&bprm->file->f_path, realpath, PATH_MAX-1);
	if (ret != 0) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT Failed creating realpath. process name=%s\n file->f_path=%s, uid=%d, pid=%d\n",
			 __FUNCTION__, __LINE__, current->comm, bprm->file->f_path.dentry->d_name.name, CURRENT_UID, current->pid);
		kfree(realpath);
		FJLSM_REJECT(ret)
	}
	if ((strncmp(realpath, CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH,
				strlen(CONFIG_SECURITY_FJSEC_SYSTEM_DIR_PATH)) == 0) ||
	     (strncmp(realpath, CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH,
				strlen(CONFIG_SECURITY_FJSEC_VENDOR_DIR_PATH)) == 0)) {
		kfree(realpath);
		return 0;
	}

	if((current->cred->euid.val != 0) && (bprm->cred->euid.val == 0)) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s, process name=%s, current euid %d request euid %d\n",
			   __FUNCTION__, __LINE__, realpath, current->comm,current->cred->euid.val, bprm->cred->euid.val);
		kfree(realpath);
		FJLSM_REJECT( -EPERM)
	}

	if((current->cred->egid.val != 0) && (bprm->cred->egid.val == 0)) {
		printk(KERN_INFO "%s:%d: FJLSM_REJECT realpath=%s, process name=%s, current egid %d request egid %d\n",
			   __FUNCTION__, __LINE__, realpath, current->comm,current->cred->egid.val, bprm->cred->egid.val);
		kfree(realpath);
		FJLSM_REJECT( -EPERM)
	}

	kfree(realpath);
	return 0;
}


static struct security_hook_list fjsec_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(sb_mount, fjsec_sb_mount),
	LSM_HOOK_INIT(sb_umount, fjsec_sb_umount),
	LSM_HOOK_INIT(sb_pivotroot, fjsec_sb_pivotroot),
	LSM_HOOK_INIT(file_permission, fjsec_file_permission),
	LSM_HOOK_INIT(file_open, fjsec_file_open),
	LSM_HOOK_INIT(task_fix_setuid, fjsec_task_fix_setuid),
	LSM_HOOK_INIT(task_fix_setgid, fjsec_task_fix_setgid),
	LSM_HOOK_INIT(bprm_set_creds, fjsec_bprm_set_creds),
#ifdef CONFIG_SECURITY_PATH
	LSM_HOOK_INIT(path_mknod, fjsec_path_mknod),
	LSM_HOOK_INIT(path_mkdir, fjsec_path_mkdir),
	LSM_HOOK_INIT(path_rmdir, fjsec_path_rmdir),
	LSM_HOOK_INIT(path_unlink, fjsec_path_unlink),
	LSM_HOOK_INIT(path_symlink, fjsec_path_symlink),
	LSM_HOOK_INIT(path_link, fjsec_path_link),
	LSM_HOOK_INIT(path_rename, fjsec_path_rename),
	LSM_HOOK_INIT(path_truncate, fjsec_path_truncate),
	LSM_HOOK_INIT(path_chmod, fjsec_path_chmod),
	LSM_HOOK_INIT(path_chown, fjsec_path_chown),
#ifdef CONFIG_SECURITY_FJSEC_PROTECT_CHROOT
	LSM_HOOK_INIT(path_chroot, fjsec_path_chroot),
#endif	/* CONFIG_SECURITY_FJSEC_PROTECT_CHROOT */
#endif	/* CONFIG_SECURITY_PATH */
};


static int __init fjsec_init (void)
{
#if defined(CONFIG_STRICT_MEMORY_RWX_EX) && !defined(CONFIG_LSM_ON)
	printk(KERN_INFO "%s:%d: fjsec_hooks=0x%16lx\n"
			, __FUNCTION__, __LINE__, (unsigned long)(&fjsec_hooks[0]));
#endif // #if defined(CONFIG_STRICT_MEMORY_RWX_EX) && !defined(CONFIG_LSM_ON)

	security_add_hooks(fjsec_hooks, ARRAY_SIZE(fjsec_hooks),"fjsec");

	printk (KERN_INFO "FJSEC LSM module initialized\n");

	return 0;
}

static int __init setup_fjlsm_mode(char *str)
{
	lsm_nv_flag = FJSEC_LSM_NV_AFTER;
	lsm_enable_flag = FJSEC_LSM_ENABLED;

	if (strcmp(str, BOOT_ARGS_FJLSM_ENFORCING) == 0) {
		lsm_enable_flag = FJSEC_LSM_ENABLED;
	} else if (strcmp(str, BOOT_ARGS_FJLSM_PERMISSIVE) == 0) {
		lsm_enable_flag = FJSEC_LSM_PERMISSIVE;
	} else if (strcmp(str, BOOT_ARGS_FJLSM_OFF) == 0) {
		lsm_enable_flag = FJSEC_LSM_DISABLED;
	}
#if defined(LSM_OFF)
	lsm_enable_flag = FJSEC_LSM_DISABLED;
#endif
	printk(KERN_INFO "FJLSM FLAG=<%d>\n", (int)lsm_enable_flag);
	return 0;
}
early_param("lsm.mode", setup_fjlsm_mode);

security_initcall (fjsec_init);
