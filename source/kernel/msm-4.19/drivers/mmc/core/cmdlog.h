/*----------------------------------------------------------------------------*/
/* (C) 2021 FCNT LIMITED              */
/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MMC_CORE_CMDLOG_H
#define _MMC_CORE_CMDLOG_H

#define MMC_PRINT_RESPONSE(host, mrq)			mmc_cmdlog_print_response(host, mrq)
#define MMC_PRINT_CMDLOG(host, mmc_rq)	mmc_cmdlog_print(host, mmc_rq)

void mmc_cmdlog_sysfs_init(struct mmc_host *host);

void mmc_cmdlog_dump_init(struct mmc_host *host);
void mmc_cmdlog_dump(struct mmc_host *host);

void mmc_cmdlog_print(struct mmc_host *host, struct mmc_request *mmc_rq);
void mmc_cmdlog_print_response(struct mmc_host *host, struct mmc_request *mrq);

#endif
