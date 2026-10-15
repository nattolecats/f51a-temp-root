/*----------------------------------------------------------------------------*/
/* (C) 2021 FCNT LIMITED              */
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0


//==============================================================================
// include file
//==============================================================================

#include <linux/jiffies.h>
#include <linux/rtc.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

//#include "../card/queue.h"

#include "core.h"
#include "bus.h"
#include "host.h"
#include "sdio_bus.h"

#include "mmc_ops.h"
#include "sd_ops.h"
#include "sdio_ops.h"

 #include "cmdlog.h"

//==============================================================================
// define / macro
//==============================================================================
#define MMC_CMDLOG_TIMESTAMP_LEN				(30)
#define MMC_CMDLOG_FILTER_LEN					(4)
#define MMC_IS_PRINTK_CMDLOG_ENABLE()			(cmdlog_flag)
#define MMC_CHECK_PRINTK_CMDLOG_FILTER()		((strlen(filter) == 0) || ((strlen(filter) > 0) && (strcmp(mmc_hostname(host), filter) == 0)))
#define MMC_PRINTK_SIZE							(512)
#define MMC_CMDLOG_BUF_SIZE						(16*1024)
#define MMC_CMDLOG_TEMPBUF_SIZE					(512)
#define MMC_CMDLOG_DEV_NUM						(1)

#define cls_dev_to_mmc_host(d)	container_of(d, struct mmc_host, class_dev)
#define MMC_TRACE_CHAR_LEN						(16)

//==============================================================================
// struct
//==============================================================================
struct mmc_cmdlog {
	char 			buf[MMC_CMDLOG_BUF_SIZE];
	char 			buf_temp[MMC_CMDLOG_TEMPBUF_SIZE];
	spinlock_t		lock;
	bool 			lock_initialized;
	bool 			head_move;
	unsigned long	end_pos;
};

//==============================================================================
// static data
//==============================================================================
static struct mmc_cmdlog	mmc_cmdlog_data[MMC_CMDLOG_DEV_NUM];
static int  cmdlog_flag = 0;
static char filter[MMC_CMDLOG_FILTER_LEN+1];

//==============================================================================
// functions
//==============================================================================
static ssize_t
mmc_cmdlog_dump_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);
	mmc_cmdlog_dump(host);

	return 0;
}

static ssize_t
mmc_cmdlog_dump_store(struct device *dev, struct device_attribute *attr,
		const char *data, size_t len)
{
	return len;
}

static ssize_t
mmc_cmdlog_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", cmdlog_flag);
}

static ssize_t
mmc_cmdlog_store(struct device *dev, struct device_attribute *attr,
		const char *data, size_t len)
{
	if (!strncmp(data, "1", 1)) {
		cmdlog_flag = 1;
	} else if (!strncmp(data, "0", 1)) {
		cmdlog_flag = 0;
	} else {
		return -EINVAL;
	}

	return len;
}

static ssize_t
mmc_cmdlog_filter_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%s\n", filter);
}

static ssize_t
mmc_cmdlog_filter_store(struct device *dev, struct device_attribute *attr,
		const char *data, size_t len)
{
	if (len != sizeof(filter)) {
		return -EINVAL;
	}
	memset(filter, 0, sizeof(filter));
	memcpy(filter, data, len-1);

	return len;
}

static DEVICE_ATTR(cmdlog_dump,    S_IRUGO,           mmc_cmdlog_dump_show,   mmc_cmdlog_dump_store);
static DEVICE_ATTR(cmdlog,         S_IRUGO | S_IWUSR, mmc_cmdlog_show,        mmc_cmdlog_store);
static DEVICE_ATTR(cmdlog_filter,  S_IRUGO | S_IWUSR, mmc_cmdlog_filter_show, mmc_cmdlog_filter_store);

void mmc_cmdlog_sysfs_init(struct mmc_host *host)
{
	if (mmc_host_get_type(host) == HOST_TYPE_SDIO)
		return;

	if (host->index == 0) {
		if (device_create_file(&host->class_dev, &dev_attr_cmdlog))
			pr_err("%s: Failed to create cmdlog sysfs entry\n",
					mmc_hostname(host));

		if (device_create_file(&host->class_dev, &dev_attr_cmdlog_filter))
			pr_err("%s: Failed to create cmdlog_filter sysfs entry\n",
					mmc_hostname(host));
	}
	if (device_create_file(&host->class_dev, &dev_attr_cmdlog_dump))
		pr_err("%s: Failed to create cmdlog_dump_filter sysfs entry\n",
				mmc_hostname(host));

	return;
}

void mmc_cmdlog_dump_init(struct mmc_host *host)
{
	struct mmc_cmdlog * cmdlog;

	if (host->index >= MMC_CMDLOG_DEV_NUM)
		return;

	cmdlog = &mmc_cmdlog_data[host->index];

	if (!cmdlog->lock_initialized) {
		spin_lock_init( &cmdlog->lock );
		cmdlog->lock_initialized = true;
	}
	cmdlog->head_move = 0;
	cmdlog->end_pos = 0;

	return;
}

static void mmc_cmdlog_printf_internal( struct mmc_cmdlog * cmdlog, bool print_flg, const char* format, va_list args )
{
	unsigned log_size = 0;
	unsigned long flags;

	spin_lock_irqsave(&cmdlog->lock, flags);

	/* write in the temp buffer */
	log_size = vsnprintf( &cmdlog->buf_temp[0], sizeof(cmdlog->buf_temp), format, args );

	if (log_size > 0 && log_size <= MMC_CMDLOG_BUF_SIZE) {

		if ((cmdlog->end_pos + log_size) > MMC_CMDLOG_BUF_SIZE) {
			/* The excess buffer is cleared */
			memset( &cmdlog->buf[ cmdlog->end_pos ], 0, MMC_CMDLOG_BUF_SIZE - cmdlog->end_pos );

			/* Return to Top */
			cmdlog->end_pos = 0;
			cmdlog->head_move = true;
		}
		memcpy( &cmdlog->buf[ cmdlog->end_pos ], cmdlog->buf_temp, log_size );
		cmdlog->end_pos += log_size;
	}

	if (cmdlog->end_pos >= MMC_CMDLOG_BUF_SIZE)
		cmdlog->end_pos = 0;

	if (print_flg)
		printk(KERN_ERR "%s", &cmdlog->buf_temp[0]);
	spin_unlock_irqrestore( &cmdlog->lock, flags );

	return;
}

static void mmc_cmdlog_printf( struct mmc_host *host, const char* format, ... )
{
	va_list args;
	bool print_flg = false;

	if (host->index >= MMC_CMDLOG_DEV_NUM)
		return;

	if (MMC_IS_PRINTK_CMDLOG_ENABLE() && MMC_CHECK_PRINTK_CMDLOG_FILTER())
		print_flg = true;

	va_start( args, format );
	mmc_cmdlog_printf_internal( &mmc_cmdlog_data[host->index], print_flg, format, args );
	va_end( args );

	return;
}

static void mmc_cmdlog_kmsg_output( char *msg, unsigned long size )
{
	int print_size = 0;
	int print_size_total = 0;
	unsigned char *buf = msg;
	unsigned char *lnptr = NULL;

	for (print_size_total = 0; print_size_total < size; print_size_total += print_size) {
		lnptr = strchr(buf, '\n');

		if (lnptr) {
			print_size = lnptr - buf + 1;
			printk( KERN_CONT "%.*s", print_size, buf);
			buf += print_size;
		} else if (strlen(buf) > 0) {
			printk( "%s\n", buf);
			break;
		}
	}

	return;
}

void mmc_cmdlog_dump( struct mmc_host *host )
{
	unsigned long flags;
	struct mmc_cmdlog * cmdlog;

	if (host->index >= MMC_CMDLOG_DEV_NUM)
		return;

	if (MMC_IS_PRINTK_CMDLOG_ENABLE())
		return;

	cmdlog = &mmc_cmdlog_data[host->index];
	if (!cmdlog->lock_initialized)
		return;

	spin_lock_irqsave( &cmdlog->lock, flags );

	if (cmdlog->head_move)
		mmc_cmdlog_kmsg_output( cmdlog->buf + cmdlog->end_pos, MMC_CMDLOG_BUF_SIZE - cmdlog->end_pos );
	mmc_cmdlog_kmsg_output( cmdlog->buf, cmdlog->end_pos );

	cmdlog->head_move = 0;
	cmdlog->end_pos = 0;

	spin_unlock_irqrestore( &cmdlog->lock, flags );

	return;
}

static void mmc_cmdlog_get_timestamp(char *buf_p)
{
	struct timespec ts;
	struct rtc_time tm;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);

	snprintf(buf_p, MMC_CMDLOG_TIMESTAMP_LEN, "%d-%02d-%02d %02d:%02d:%02d.%09lu",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);

	return;
}

#if 0
static void mmc_cmdlog_print_packed_list(struct mmc_host *host, struct mmc_async_req *areq, char * time)
{
	struct mmc_queue_req *mq_rq;
	int packed_num, rw, version, i, index = 1;
	char * cmd = NULL;

	mq_rq = container_of(areq, struct mmc_queue_req, mmc_active);
	if (!mq_rq)
		return;

	version		= (mq_rq->packed->cmd_hdr[0] & 0xFF);
	rw			= ((mq_rq->packed->cmd_hdr[0] & 0xFF00) >> 8);
	packed_num	= ((mq_rq->packed->cmd_hdr[0] & 0xFF0000) >> 16);
	if (rw == 0x02)
		cmd = "CMD25";
	else if(rw == 0x01)
		cmd = "CMD18";

	mmc_cmdlog_printf(host, "%s [CMD LOG]%s: ------------Packed Header------------- \n", time, mmc_hostname(host));
	mmc_cmdlog_printf(host, "%s [CMD LOG]%s: | PACKED VERSION 0x%x R/W 0x%x NUM 0x%x \n",
			time, mmc_hostname(host), version, rw, packed_num);
	for (i=0; i < packed_num; i++) {
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s: | CMD23_ARG 0x%08x %s_ARG 0x%08x \n",
				time, mmc_hostname(host), mq_rq->packed->cmd_hdr[(index * 2)], cmd, mq_rq->packed->cmd_hdr[(index * 2) + 1]);
		index++;
	}
	mmc_cmdlog_printf(host, "%s [CMD LOG]%s: -------------------------------------- \n", time, mmc_hostname(host));
	return;
}
#endif

void mmc_cmdlog_print(struct mmc_host *host, struct mmc_request *mmc_rq)
{
	char	buf[MMC_CMDLOG_TIMESTAMP_LEN];

	if (mmc_host_get_type(host) == HOST_TYPE_SDIO)
		return;

	mmc_cmdlog_get_timestamp(&buf[0]);
	if (mmc_rq->sbc) {
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s: CMD%u arg 0x%08x\n",
			 &buf[0], mmc_hostname(host), mmc_rq->sbc->opcode, mmc_rq->sbc->arg);
	}

	if (mmc_rq->cmd)
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s: CMD%u arg 0x%08x\n",
			 &buf[0], mmc_hostname(host), mmc_rq->cmd->opcode, mmc_rq->cmd->arg);

	if (mmc_rq->data) {
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s:       blksz %d blocks %d\n",
			&buf[0], mmc_hostname(host), mmc_rq->data->blksz,
			mmc_rq->data->blocks);
	}

	if (mmc_rq->stop && !mmc_rq->sbc) {
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s: CMD%u arg 0x%08x\n",
			 &buf[0], mmc_hostname(host), mmc_rq->stop->opcode, mmc_rq->stop->arg);
	}
	return;
}

void mmc_cmdlog_print_response(struct mmc_host *host, struct mmc_request *mrq)
{
	char	buf[MMC_CMDLOG_TIMESTAMP_LEN];
	struct mmc_command *cmd = mrq->cmd;

	if (mmc_host_get_type(host) == HOST_TYPE_SDIO)
		return;

	mmc_cmdlog_get_timestamp(&buf[0]);
	mmc_cmdlog_printf(host, "%s [CMD LOG]%s: CMD%u response 0x%08x 0x%08x 0x%08x 0x%08x\n",
		&buf[0], mmc_hostname(host), cmd->opcode,
		cmd->resp[0], cmd->resp[1],
		cmd->resp[2], cmd->resp[3]);

	if (mrq->stop && !mrq->sbc) {
		mmc_cmdlog_printf(host, "%s [CMD LOG]%s: CMD%u response 0x%08x 0x%08x 0x%08x 0x%08x\n",
			&buf[0], mmc_hostname(host), mrq->stop->opcode,
			mrq->stop->resp[0], mrq->stop->resp[1],
			mrq->stop->resp[2], mrq->stop->resp[3]);
	}
	return;
}

