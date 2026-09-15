/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012, 2014-2017, 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_MEMORY_DUMP_H
#define __MSM_MEMORY_DUMP_H

#include <linux/errno.h>
#include <linux/types.h>

/**
 * MSM_DUMP_MAKE_VERSION() - Encode a major/minor pair into a dump table version.
 * @ma: Major version number.
 * @mi: Minor version number.
 */
#define MSM_DUMP_MAKE_VERSION(ma, mi)	(((ma) << 20) | (mi))

/* Maximum number of entries supported by a dump table. */
#define MAX_NUM_ENTRIES		0x150

/**
 * enum msm_dump_data_ids - dump data identifiers.
 *
 * These identifiers are used in the dump table to
 * identify the data captured by each dump entry.
 * The values form part of the dump format
 * consumed by firmware and must not be changed.
 *
 * @MSM_DUMP_DATA_CPU_CTX: CPU register context.
 * @MSM_DUMP_DATA_L1_INST_CACHE: L1 instruction cache.
 * @MSM_DUMP_DATA_L1_DATA_CACHE: L1 data cache.
 * @MSM_DUMP_DATA_ETM_REG: ETM trace macrocell registers.
 * @MSM_DUMP_DATA_L2_CACHE: L2 cache.
 * @MSM_DUMP_DATA_L3_CACHE: L3 cache.
 * @MSM_DUMP_DATA_OCMEM: On-Chip memory.
 * @MSM_DUMP_DATA_CNSS_WLAN: WLAN subsystem state.
 * @MSM_DUMP_DATA_WIGIG: WiGig subsystem state.
 * @MSM_DUMP_DATA_PMIC: PMIC register state.
 * @MSM_DUMP_DATA_DBGUI_REG: DebugUI registers.
 * @MSM_DUMP_DATA_DCC_REG: Data Capture and Compare registers.
 * @MSM_DUMP_DATA_DCC_SRAM: Data Capture and Compare SRAM contents.
 * @MSM_DUMP_DATA_MISC: Miscellaneous region, driver-defined contents.
 * @MSM_DUMP_DATA_VSENSE: Voltage sensor state.
 * @MSM_DUMP_DATA_RPM: Resource Power Manager state.
 * @MSM_DUMP_DATA_SCANDUMP: Scan-dump register capture.
 * @MSM_DUMP_DATA_RPMH: RPMh state.
 * @MSM_DUMP_DATA_TMC_ETF: TMC-ETF trace buffer.
 * @MSM_DUMP_DATA_TMC_ETF_SWAO: TMC-ETF trace buffer, always-on domain.
 * @MSM_DUMP_DATA_TMC_REG: TMC registers.
 * @MSM_DUMP_DATA_TMC_ETF_SWAO_REG: TMC-ETF registers, always-on domain.
 * @MSM_DUMP_DATA_LOG_BUF: Kernel log buffer.
 * @MSM_DUMP_DATA_LOG_BUF_FIRST_IDX: Kernel log buffer first index.
 * @MSM_DUMP_DATA_SCANDUMP_PER_CPU: Per-CPU scan-dump registers capture.
 * @MSM_DUMP_DATA_LLCC_PER_INSTANCE: Per-instance LLCC state.
 * @MSM_DUMP_DATA_MAX: Upper bound, equal to %MAX_NUM_ENTRIES.
 */
enum msm_dump_data_ids {
	MSM_DUMP_DATA_CPU_CTX = 0x00,
	MSM_DUMP_DATA_L1_INST_CACHE = 0x60,
	MSM_DUMP_DATA_L1_DATA_CACHE = 0x80,
	MSM_DUMP_DATA_ETM_REG = 0xA0,
	MSM_DUMP_DATA_L2_CACHE = 0xC0,
	MSM_DUMP_DATA_L3_CACHE = 0xD0,
	MSM_DUMP_DATA_OCMEM = 0xE0,
	MSM_DUMP_DATA_CNSS_WLAN = 0xE1,
	MSM_DUMP_DATA_WIGIG = 0xE2,
	MSM_DUMP_DATA_PMIC = 0xE4,
	MSM_DUMP_DATA_DBGUI_REG = 0xE5,
	MSM_DUMP_DATA_DCC_REG = 0xE6,
	MSM_DUMP_DATA_DCC_SRAM = 0xE7,
	MSM_DUMP_DATA_MISC = 0xE8,
	MSM_DUMP_DATA_VSENSE = 0xE9,
	MSM_DUMP_DATA_RPM = 0xEA,
	MSM_DUMP_DATA_SCANDUMP = 0xEB,
	MSM_DUMP_DATA_RPMH = 0xEC,
	MSM_DUMP_DATA_TMC_ETF = 0xF0,
	MSM_DUMP_DATA_TMC_ETF_SWAO = 0xF1,
	MSM_DUMP_DATA_TMC_REG = 0x100,
	MSM_DUMP_DATA_TMC_ETF_SWAO_REG = 0x102,
	MSM_DUMP_DATA_LOG_BUF = 0x110,
	MSM_DUMP_DATA_LOG_BUF_FIRST_IDX = 0x111,
	MSM_DUMP_DATA_SCANDUMP_PER_CPU = 0x130,
	MSM_DUMP_DATA_LLCC_PER_INSTANCE = 0x140,
	MSM_DUMP_DATA_MAX = MAX_NUM_ENTRIES,
};

/**
 * enum msm_dump_table_ids - dump table identifiers.
 * @MSM_DUMP_TABLE_APPS: Applications processor dump table.
 * @MSM_DUMP_TABLE_MAX: Upper bound, equal to %MAX_NUM_ENTRIES.
 */
enum msm_dump_table_ids {
	MSM_DUMP_TABLE_APPS,
	MSM_DUMP_TABLE_MAX = MAX_NUM_ENTRIES,
};

/**
 * enum msm_dump_type - dump entry types.
 * @MSM_DUMP_TYPE_DATA: Entry references dump data.
 * @MSM_DUMP_TYPE_TABLE: Entry references dump table.
 */
enum msm_dump_type {
	MSM_DUMP_TYPE_DATA,
	MSM_DUMP_TYPE_TABLE,
};

/**
 * struct msm_dump_data - dump data descriptor.
 * @version: Dump data version.
 * @magic: Magic value.
 * @name: Dump data name, copied from the owning device-tree node.
 * @addr: Physical address of the dump buffer.
 * @len: Dump buffer size in bytes.
 * @reserved: padding.
 */
struct msm_dump_data {
	u32 version;
	u32 magic;
	char name[32];
	u64 addr;
	u64 len;
	u32 reserved;
};

/**
 * struct msm_dump_entry - memory-dump entry descriptor.
 * @id: Dump data or table identifier.
 * @name: Entry name.
 * @type: Entry type, see &enum msm_dump_type.
 * @addr: Physical address of the referenced dump data or table.
 */
struct msm_dump_entry {
	u32 id;
	char name[32];
	u32 type;
	u64 addr;
};

#endif
