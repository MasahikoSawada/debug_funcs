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
#include "fmgr.h"

#define MULTI_EXEC_KEY 500

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_LockBufferForCleanup);
PG_FUNCTION_INFO_V1(pg_LockBuffer);
PG_FUNCTION_INFO_V1(pg_lockforextension);
PG_FUNCTION_INFO_V1(replock);
PG_FUNCTION_INFO_V1(show_define_variables);
PG_FUNCTION_INFO_V1(pg_get_lock);
PG_FUNCTION_INFO_V1(rel_lock);
PG_FUNCTION_INFO_V1(rel_unlock);
PG_FUNCTION_INFO_V1(rel_lock_unlock);
PG_FUNCTION_INFO_V1(hoge);

Datum
hoge(PG_FUNCTION_ARGS)
{
	Datum array[4];
	int nelems = 0;

	Datum res;
	ArrayType *a;
	char *a_string;

	array[0] = CStringGetTextDatum("hoge");
	array[1] = CStringGetTextDatum("bar");
	array[2] = CStringGetTextDatum("rar");
	array[3] = CStringGetTextDatum("yah");
	nelems = 4;
	
	a = construct_array(array, nelems, TEXTOID, -1, false, 'i');

	res = DirectFunctionCall1(array_out, PointerGetDatum(a));

	a_string = DatumGetCString(res);
	
	elog(WARNING, "a : %s", a_string);

	PG_RETURN_NULL();
}

Datum
rel_lock(PG_FUNCTION_ARGS)
{
	Oid	relid = PG_GETARG_OID(0);
	bool conditional = PG_GETARG_BOOL(1);
	Relation	rel;

	rel = relation_open(relid, RowExclusiveLock);

	if (conditional)
	{
		bool ret;
		ret = ConditionalLockRelationForExtension(rel, LW_EXCLUSIVE);
		elog(WARNING, "conditional ret = %d", ret);
	}
	else
		LockRelationForExtension(rel, LW_EXCLUSIVE);

	relation_close(rel, RowExclusiveLock);

	PG_RETURN_NULL();
}

Datum
rel_unlock(PG_FUNCTION_ARGS)
{
	Oid	relid = PG_GETARG_OID(0);
	Relation	rel;

	rel = relation_open(relid, RowExclusiveLock);

	UnlockRelationForExtension(rel, LW_EXCLUSIVE);

	relation_close(rel, RowExclusiveLock);
	PG_RETURN_NULL();
}

Datum
rel_lock_unlock(PG_FUNCTION_ARGS)
{
	Oid	relid = PG_GETARG_OID(0);
	int sec = PG_GETARG_INT32(1);
	Relation	rel;

	rel = relation_open(relid, RowExclusiveLock);

	LockRelationForExtension(rel, LW_EXCLUSIVE);

	pg_usleep(sec * 1000L * 1000L);

	UnlockRelationForExtension(rel, LW_EXCLUSIVE);

	relation_close(rel, RowExclusiveLock);

	PG_RETURN_NULL();
}


Datum
pg_get_lock(PG_FUNCTION_ARGS)
{
	LockRelationOid(99999, AccessShareLock);

	PG_RETURN_NULL();
}

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
show_define_variables(PG_FUNCTION_ARGS)
{
	int mapsize =  BLCKSZ - MAXALIGN(SizeOfPageHeaderData);
	int heapblocks_per_byte =  BITS_PER_BYTE / BITS_PER_HEAPBLOCK;
	elog(NOTICE, "visibilitymap.c: MAPSIZE %u", mapsize);
	elog(NOTICE, "visibilitymap.c: HEAPBLOCKS_PER_BYTE %u", heapblocks_per_byte);
	elog(NOTICE, "visibilitymap.c: HEAPBLOCKS_PER_PAGE %u", mapsize * heapblocks_per_byte);

	PG_RETURN_NULL();
}
