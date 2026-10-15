/*
 * (C) 2021 FCNT LIMITED
 */
/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _SMEM_CUSTOM_H_
#define _SMEM_CUSTOM_H_

#define SMEM_OEM_V0_002_FUNC_SIZE   4096
#define SMEM_OEM_V0_009_FUNC_SIZE   128
#define SMEM_OEM_V0_015_FUNC_SIZE   256
#define SMEM_OEM_V0_018_FUNC_SIZE   16
#define SMEM_OEM_V0_020_FUNC_SIZE   5
#define SMEM_OEM_V0_021_FUNC_SIZE   5

typedef struct {
	uint32_t		smem_v0_oem_001;
	uint8_t			smem_v0_oem_002[SMEM_OEM_V0_002_FUNC_SIZE];
	uint32_t		smem_v0_oem_003;
	uint32_t		smem_v0_oem_004;
	uint32_t		smem_v0_oem_005;
	uint32_t		smem_v0_oem_006;
	uint32_t		smem_v0_oem_007;
	uint32_t		smem_v0_oem_008;
	uint8_t			smem_v0_oem_009[SMEM_OEM_V0_009_FUNC_SIZE];
	uint32_t		smem_v0_oem_010;
	uint32_t		smem_v0_oem_011;
	uint32_t		smem_v0_oem_012;
	uint32_t		smem_v0_oem_013;
	uint32_t		smem_v0_oem_014;
	uint8_t			smem_v0_oem_015[SMEM_OEM_V0_015_FUNC_SIZE];
	uint32_t		smem_v0_oem_016;
	uint32_t		smem_v0_oem_017;
	uint8_t			smem_v0_oem_018[SMEM_OEM_V0_018_FUNC_SIZE];
	uint8_t			smem_v0_oem_019;
	uint32_t		smem_v0_oem_020[SMEM_OEM_V0_020_FUNC_SIZE];
	uint32_t		smem_v0_oem_021[SMEM_OEM_V0_021_FUNC_SIZE];
} smem_vendor0_data_type;

typedef enum
{
	SMEM_OEM_START_ID_V0 = 0,
	SMEM_OEM_V0_001,
	SMEM_OEM_V0_002,
	SMEM_OEM_V0_003,
	SMEM_OEM_V0_004,
	SMEM_OEM_V0_005,
	SMEM_OEM_V0_006,
	SMEM_OEM_V0_007,
	SMEM_OEM_V0_008,
	SMEM_OEM_V0_009,
	SMEM_OEM_V0_010,
	SMEM_OEM_V0_011,
	SMEM_OEM_V0_012,
	SMEM_OEM_V0_013,
	SMEM_OEM_V0_014,
	SMEM_OEM_V0_015,
	SMEM_OEM_V0_016,
	SMEM_OEM_V0_017,
	SMEM_OEM_V0_018,
	SMEM_OEM_V0_019,
	SMEM_OEM_V0_020,
	SMEM_OEM_V0_021,
	SMEM_OEM_LAST_ID_V0
} smem_mem_vendor0_type;

void *smem_alloc_vendor0(smem_mem_vendor0_type smem_v0_type);
#endif /* _SMEM_CUSTOM_H_ */
