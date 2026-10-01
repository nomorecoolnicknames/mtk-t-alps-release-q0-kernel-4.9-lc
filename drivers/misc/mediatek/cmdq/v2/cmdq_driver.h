/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __CMDQ_DRIVER_H__
#define __CMDQ_DRIVER_H__

#include "cmdq_def.h"
#include <linux/kernel.h>

struct cmdqUsageInfoStruct {
	uint32_t count
		[CMDQ_MAX_ENGINE_COUNT]; /* [OUT] current engine ref count */
};

struct cmdqJobStruct {
	struct cmdqCommandStruct command; /* [IN] the job to perform */
	cmdqJobHandle_t hJob;		  /* [OUT] handle to resulting job */
};

struct cmdqJobResultStruct {
	/* [IN]  Job handle from CMDQ_IOCTL_ASYNC_JOB_EXEC */
	cmdqJobHandle_t	hJob;
	/* [OUT] engine flag passed down originally */
	uint64_t engineFlag;

	/* [IN/OUT] read register values, if any. */
	/* as input, the "count" field must represent */
	/* buffer space pointed by "regValues". */
	/* Upon return, CMDQ driver fills "count" with */
	/* actual requested register count. */
	/* However, if the input "count" is too small, */
	/* -ENOMEM is returned, and "count" is filled */
	/* with requested register count. */
	struct cmdqRegValueStruct regValue;

	struct cmdqReadAddressStruct
		readAddress; /* [IN/OUT] physical address to read */
};

struct cmdqWriteAddressStruct {
	/* [IN] count of the writable buffer (unit is # of uint32_t, NOT in
	 * byte)
	 */
	uint32_t count;

	/* [OUT] When Alloc, this is the resulting PA. It is guaranteed to be
	 * continuous.
	 */
	/* [IN]  When Free, please pass returned address down to ioctl. */
	/*  */
	/* indeed param startPA should be UNSIGNED LONG type for 64 bit kernel.
	 */
	/* Considering our plartform supports max 4GB RAM(upper-32bit don't care
	 * for SW)
	 */
	/* and consistent common code interface, remain uint32_t type. */
	uint32_t startPA;
};

#define CMDQ_IOCTL_MAGIC_NUMBER 'x'

#define CMDQ_IOCTL_LOCK_MUTEX _IOW(CMDQ_IOCTL_MAGIC_NUMBER, 1, int)
#define CMDQ_IOCTL_UNLOCK_MUTEX _IOR(CMDQ_IOCTL_MAGIC_NUMBER, 2, int)
#define CMDQ_IOCTL_EXEC_COMMAND                                                \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 3, struct cmdqCommandStruct)
#define CMDQ_IOCTL_QUERY_USAGE                                                 \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 4, struct cmdqUsageInfoStruct)

/*  */
/* Async operations */
/*  */
#define CMDQ_IOCTL_ASYNC_JOB_EXEC                                              \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 5, struct cmdqJobStruct)

/* forge m5c (camera lane, 2026-09-03): the m5c userspace blobs were built
 * against a cmdqCommandStruct WITHOUT the trailing userDebugStr /
 * userDebugStrLen pair, so their cmdqJobStruct is 16 bytes shorter and the
 * size baked into _IOW() does not match ours.  The kernel then rejects the
 * call outright - "[CMDQ][ERR][COMPAT]unrecognized ioctl 0x40d87805" - MDP
 * cannot submit its command block, its job queue fills (DP_STATUS_BUFFER_FULL),
 * pass2 never starts and the camera preview shows an all-zero (green) buffer.
 *
 * Measured from the shipped kernel's DWARF, not guessed:
 *   sizeof(struct cmdqJobStruct)          = 232   (ours)
 *   offsetof(cmdqCommandStruct, userDebugStr) = 208
 *   offsetof(cmdqJobStruct, hJob)         = 224
 * so the blob layout is a strict PREFIX of ours: command truncated at 208,
 * hJob at 208, total 216 - exactly the size seen on the wire.
 *
 * Same shape as the fix forge p48 made for CMDQ_IOCTL_QUERY_DTS below.  The
 * number is derived from offsetof so it can never drift away from the struct.
 */
#define CMDQ_JOB_LEGACY_CMD_SIZE \
	offsetof(struct cmdqCommandStruct, userDebugStr)
#define CMDQ_JOB_LEGACY_SIZE \
	(CMDQ_JOB_LEGACY_CMD_SIZE + sizeof(cmdqJobHandle_t))
#define CMDQ_IOCTL_ASYNC_JOB_EXEC_LEGACY \
	_IOC(_IOC_WRITE, CMDQ_IOCTL_MAGIC_NUMBER, 5, CMDQ_JOB_LEGACY_SIZE)
#define CMDQ_IOCTL_ASYNC_JOB_WAIT_AND_CLOSE                                    \
	_IOR(CMDQ_IOCTL_MAGIC_NUMBER, 6, struct cmdqJobResultStruct)

#define CMDQ_IOCTL_ALLOC_WRITE_ADDRESS                                         \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 7, struct cmdqWriteAddressStruct)
#define CMDQ_IOCTL_FREE_WRITE_ADDRESS                                          \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 8, struct cmdqWriteAddressStruct)
#define CMDQ_IOCTL_READ_ADDRESS_VALUE                                          \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 9, struct cmdqReadAddressStruct)

/*  */
/* Chip capability query. output parameter is a bit field. */
/* Bit definition is enum CMDQ_CAP_BITS. */
/*  */
#define CMDQ_IOCTL_QUERY_CAP_BITS _IOW(CMDQ_IOCTL_MAGIC_NUMBER, 10, int)

/*  */
/* HW info. from DTS */
/*  */
#define CMDQ_IOCTL_QUERY_DTS                                                   \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 11, struct cmdqDTSDataStruct)

/* forge p48: 27-subsys layout the m5c userspace blobs were built against */
#define CMDQ_IOCTL_QUERY_DTS_LEGACY                                            \
	_IOW(CMDQ_IOCTL_MAGIC_NUMBER, 11, struct cmdqDTSDataStruct_legacy)

/*  */
/* Notify MDP will use specified engine before really use. */
/* input int is same as EngineFlag. */
/*  */
#define CMDQ_IOCTL_NOTIFY_ENGINE _IOW(CMDQ_IOCTL_MAGIC_NUMBER, 12, uint64_t)

#endif /* __CMDQ_DRIVER_H__ */
