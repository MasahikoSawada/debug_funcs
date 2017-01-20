/*-------------------------------------------------------------------------
 *
 * debug_funcs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/buf_internals.h"
#include "storage/bufmgr.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "access/htup_details.h"
#include "access/heapam.h"
#include "access/parallel.h"
#include "access/relscan.h"
#include "access/xact.h"
#include "access/visibilitymap.h"
#include "catalog/pg_type.h"
#include "catalog/storage_xlog.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "storage/procarray.h"
#include "storage/lmgr.h"
#include "storage/smgr.h"
#include "storage/spin.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/rel.h"

#define MULTI_EXEC_KEY 500

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_LockBufferForCleanup);
PG_FUNCTION_INFO_V1(pg_LockBuffer);
PG_FUNCTION_INFO_V1(pg_lockforextension);
PG_FUNCTION_INFO_V1(replock);
PG_FUNCTION_INFO_V1(multi_exec);
PG_FUNCTION_INFO_V1(show_define_variables);

static void multi_exec_worker(dsm_segment *seg, shm_toc *toc);

/*
 * Call LcokBufferForCleanup() function.
 */
Datum
pg_LockBufferForCleanup(PG_FUNCTION_ARGS)
{
	Oid		relid = PG_GETARG_OID(0);
	int64	blkno = PG_GETARG_INT64(1);
	int		sleep_time = PG_GETARG_INT32(2);
	Relation	rel;
	Buffer		buf;
	BufferDesc	*bufHdr;
	Page	page;
	uint32		buf_state;
	BufferAccessStrategy	bstrategy;
	
	rel = relation_open(relid, RowExclusiveLock);
	bstrategy = GetAccessStrategy(BAS_VACUUM);
	buf = ReadBufferExtended(rel, MAIN_FORKNUM, blkno,
							 RBM_NORMAL, bstrategy);
	bufHdr = GetBufferDescriptor(buf - 1);
	page = BufferGetPage(buf);
	
	buf_state = LockBufHdr(bufHdr);
	UnlockBufHdr(bufHdr, buf_state);
		
	LockBufferForCleanup(buf);
	pg_usleep(sleep_time * 1000L * 1000L);
	UnlockReleaseBuffer(buf);

	relation_close(rel, RowExclusiveLock);
	
	PG_RETURN_NULL();
}

Datum
pg_LockBuffer(PG_FUNCTION_ARGS)
{
	Oid		relid = PG_GETARG_OID(0);
	int64	blkno = PG_GETARG_INT64(1);
	text	*mode = PG_GETARG_TEXT_P(2);
	int		sleep_time = PG_GETARG_INT32(3);
	char	*mode_string = text_to_cstring(mode);
	Relation	rel;
	Buffer		buf;
	BufferAccessStrategy	bstrategy;

	rel = relation_open(relid, RowExclusiveLock);
	bstrategy = GetAccessStrategy(BAS_VACUUM);
	buf = ReadBufferExtended(rel, MAIN_FORKNUM, blkno,
							 RBM_NORMAL, bstrategy);
	
	if (strcmp(mode_string, "share") == 0)
		LockBuffer(buf, BUFFER_LOCK_SHARE);
	else if (strcmp(mode_string, "exclusive") == 0)
		LockBuffer(buf, BUFFER_LOCK_EXCLUSIVE);
	else
		elog(ERROR, "Invalid lock mode: \"%s\". Must be \"share\" or \"exclusive\"", mode_string);

	pg_usleep(sleep_time * 1000L * 1000L);
	UnlockReleaseBuffer(buf);

	relation_close(rel, RowExclusiveLock);
	
	PG_RETURN_NULL();
}

Datum
pg_lockforextension(PG_FUNCTION_ARGS)
{
	Oid		relid = PG_GETARG_OID(0);
	Relation rel;
	rel = relation_open(relid, RowExclusiveLock);

	LockRelationForExtension(rel, ExclusiveLock);

	elog(NOTICE, "before sleep");
	pg_usleep(10 * 1000L * 1000L);
	elog(NOTICE, "after slept");

	relation_close(rel, RowExclusiveLock);

	PG_RETURN_NULL();
}

Datum
replock(PG_FUNCTION_ARGS)
{
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	elog(NOTICE, "<%d> Acquired", MyProcPid);
	pg_usleep(30 * 1000L * 1000L);
	LWLockRelease(SyncRepLock);
	elog(NOTICE, "<%d> Acquired", MyProcPid);

	PG_RETURN_NULL();
}

Datum
multi_exec(PG_FUNCTION_ARGS)
{
	int nworkers = PG_GETARG_INT32(0);
	int relid = PG_GETARG_OID(1);
	ParallelContext *pcxt;

	int size = 0;
	int keys = 0;
	Oid *shm_area;

	/* Begin parallel mode */
	EnterParallelMode();

	pcxt = CreateParallelContext(multi_exec_worker, nworkers);

	size += BUFFERALIGN(sizeof(int));
	keys++;
	shm_toc_estimate_chunk(&pcxt->estimator, size);
	shm_toc_estimate_keys(&pcxt->estimator, keys); /* foo count and bar cound */
	InitializeParallelDSM(pcxt);

	/* Set up DSM */
	shm_area = (Oid *) shm_toc_allocate(pcxt->toc, sizeof(Oid));
	shm_toc_insert(pcxt->toc, MULTI_EXEC_KEY, shm_area);
	*shm_area = relid;

	/* Do foo bar */
	LaunchParallelWorkers(pcxt);

	/* Wait for parallel worker finish */
	WaitForParallelWorkersToFinish(pcxt);

	/* Finalize parallel scanning */
	DestroyParallelContext(pcxt);
	ExitParallelMode();

	PG_RETURN_NULL();
}

static void
multi_exec_worker(dsm_segment *seg, shm_toc *toc)
{
	Oid relid;
	Oid *shm_area;
	Relation rel;

	/* Look up for foo count */
	shm_area = (Oid *) shm_toc_lookup(toc, MULTI_EXEC_KEY);
	relid = *shm_area;

	rel = relation_open(relid, AccessExclusiveLock);

	if (ParallelWorkerNumber == 1)
	{
		/* 1-2. Sleep */
		fprintf(stderr, "Nubmer 1:%d sleep 10 \n", MyProcPid);
		pg_usleep(10 * 1000L * 1000L);
	}

	/* 2-2 worker 1wait for worker 0 */
	LockRelationForExtension(rel , ExclusiveLock);

	if (ParallelWorkerNumber == 0)
	{
		/* 1-1. Take lock first */
		fprintf(stderr, "Nubmer 0:%d grub a lock first sleep 30\n", MyProcPid);

		pg_usleep(30 * 1000L * 1000L);
	}
	else if (ParallelWorkerNumber == 1)
	{
		/* 2-2 could acquire lock */
		fprintf(stderr, "Nubmer 1:%d Got lock\n", MyProcPid);
	}

	UnlockRelationForExtension(rel, ExclusiveLock);

	if (ParallelWorkerNumber == 0)
	{
		/* 2-3 release lock */
		fprintf(stderr, "Nubmer 0:%d release lock\n", MyProcPid);
	}
	else if (ParallelWorkerNumber == 1)
	{
		/* 3-2 release lock */
		fprintf(stderr, "Nubmer 1:%d release lock\n", MyProcPid);
	}

	relation_close(rel, AccessExclusiveLock);
}

Datum
show_define_variables(PG_FUNCTION_ARGS)
{
	int mapsize =  BLCKSZ - MAXALIGN(SizeOfPageHeaderData);
	int heapblocks_per_byte =  BITS_PER_BYTE / BITS_PER_HEAPBLOCK;
	elog(NOTICE, "visibilitymap.c: MAPSIZE %u", mapsize);
	elog(NOTICE, "visibilitymap.c: HEAPBLOCKS_PER_BYTE %u", heapblocks_per_byte);
	elog(NOTICE, "visibilitymap.c: HEAPBLOCKS_PER_PAGE %u", mapsize * heapblocks_per_byte);

	PG_RETURN_NULL();
}
