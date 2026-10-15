/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
/*
===============================================================================================
 * HISTORY
 *
 * Tag                      Date       Author           Description
 * ======================== ========== ===============  =======================================
===============================================================================================
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "internal.h"


static DEFINE_MUTEX(smry_lock);
#define SUMMARY_LOG_ID_SIZE  928

//ssize_t pstore_write_summary(const int resetid)
ssize_t pstore_write_summary_msg(const int resetid, const char* msg)
{
	if ( psinfo != NULL ){
		struct pstore_record record;
		u64 id;
    	char buf[SUMMARY_LOG_ID_SIZE + 1];
		pstore_record_init(&record, psinfo);
    	memset(buf, 0x00, sizeof(buf));
    	sprintf(buf, "%d,%s", resetid, msg);
		record.type = PSTORE_TYPE_SUMMARY;
		record.reason = 0;
		record.part = 0;
		record.id = id;
		record.buf = (char *)&buf;
		record.compressed = 0;
		record.size = sizeof(buf);
		record.psi = psinfo;
//	mutex_lock(&smry_lock);
//	psinfo->write_buf(PSTORE_TYPE_SUMMARY, 0, &id, 0, (const char *)&resetid, 0, sizeof(int),
//	psinfo->write_buf(PSTORE_TYPE_SUMMARY, 0, &id, 0, (const char *)buf, 0, sizeof(buf),
//			  psinfo);
		psinfo->write(&record);
//	mutex_unlock(&smry_lock);
	}
	return 1;
}

ssize_t pstore_write_summary(const int resetid) {
    return pstore_write_summary_msg(resetid, "");
}
