/*
 * (C) 2021 FCNT LIMITED
 */
// SPDX-License-Identifier: GPL-2.0+
//==============================================================================
// include file
//==============================================================================
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/soc/qcom/smem.h>

#define SMEM_ID_VENDOR0	(134)

//==============================================================================
// functions
//==============================================================================
void *smem_alloc_vendor0(smem_mem_vendor0_type smem_v0_type)
{
	void						*ret_ptr = NULL;
	smem_vendor0_data_type		*smem_ptr = NULL;
	int ret;

	if ((smem_v0_type <= SMEM_OEM_START_ID_V0) || (smem_v0_type >= SMEM_OEM_LAST_ID_V0)) {
		pr_err("error: Bad number.\n");
		return NULL;
	}

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR0, sizeof(smem_vendor0_data_type));
	if (ret < 0 && ret != -EEXIST) {
		pr_err("error: unable to allocate shared state entry\n");
		return NULL;
	}
	smem_ptr = (smem_vendor0_data_type *)qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR0, NULL);
	if (IS_ERR(smem_ptr)) {
		pr_err("error: smem_ptr=NULL\n");
		return NULL;
	}

	switch (smem_v0_type) {
	case SMEM_OEM_V0_001:
		ret_ptr = &(smem_ptr->smem_v0_oem_001);
		break;
	case SMEM_OEM_V0_002:
		ret_ptr = &(smem_ptr->smem_v0_oem_002[0]);
		break;
	case SMEM_OEM_V0_003:
		ret_ptr = &(smem_ptr->smem_v0_oem_003);
		break;
	case SMEM_OEM_V0_004:
		ret_ptr = &(smem_ptr->smem_v0_oem_004);
		break;
	case SMEM_OEM_V0_005:
		ret_ptr = &(smem_ptr->smem_v0_oem_005);
		break;
	case SMEM_OEM_V0_006:
		ret_ptr = &(smem_ptr->smem_v0_oem_006);
		break;
	case SMEM_OEM_V0_007:
		ret_ptr = &(smem_ptr->smem_v0_oem_007);
		break;
	case SMEM_OEM_V0_008:
		ret_ptr = &(smem_ptr->smem_v0_oem_008);
		break;
	case SMEM_OEM_V0_009:
		ret_ptr = &(smem_ptr->smem_v0_oem_009[0]);
		break;
	case SMEM_OEM_V0_010:
		ret_ptr = &(smem_ptr->smem_v0_oem_010);
		break;
	case SMEM_OEM_V0_011:
		ret_ptr = &(smem_ptr->smem_v0_oem_011);
		break;
	case SMEM_OEM_V0_012:
		ret_ptr = &(smem_ptr->smem_v0_oem_012);
		break;
	case SMEM_OEM_V0_013:
		ret_ptr = &(smem_ptr->smem_v0_oem_013);
		break;
	case SMEM_OEM_V0_014:
		ret_ptr = &(smem_ptr->smem_v0_oem_014);
		break;
	case SMEM_OEM_V0_015:
		ret_ptr = &(smem_ptr->smem_v0_oem_015[0]);
		break;
	case SMEM_OEM_V0_016:
		ret_ptr = &(smem_ptr->smem_v0_oem_016);
		break;
	case SMEM_OEM_V0_017:
		ret_ptr = &(smem_ptr->smem_v0_oem_017);
		break;
	case SMEM_OEM_V0_018:
		ret_ptr = &(smem_ptr->smem_v0_oem_018[0]);
		break;
	case SMEM_OEM_V0_019:
		ret_ptr = &(smem_ptr->smem_v0_oem_019);
		break;
	case SMEM_OEM_V0_020:
		ret_ptr = &(smem_ptr->smem_v0_oem_020[0]);
		break;
	case SMEM_OEM_V0_021:
		ret_ptr = &(smem_ptr->smem_v0_oem_021[0]);
		break;
	default:
		/* DO NOTHING */
		break;
	}
	return ret_ptr;
}

MODULE_DESCRIPTION("MSM Shared Memory FCNT");
MODULE_AUTHOR("FUJITSU CONNECTED TECHNOLOGIES LIMITED");
MODULE_LICENSE("GPL v2");
