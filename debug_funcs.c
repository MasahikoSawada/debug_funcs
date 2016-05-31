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

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_LockBufferForCleanup);
PG_FUNCTION_INFO_V1(pg_LockBuffer);

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
	uint32		buf_state;
	BufferAccessStrategy	bstrategy;
	
	rel = relation_open(relid, RowExclusiveLock);
	bstrategy = GetAccessStrategy(BAS_VACUUM);
	buf = ReadBufferExtended(rel, MAIN_FORKNUM, blkno,
							 RBM_NORMAL, bstrategy);
	bufHdr = GetBufferDescriptor(buf - 1);
	
	buf_state = LockBufHdr(bufHdr);
	elog(NOTICE, "relation %d, blkno %d, refcount %d, usagecount %d",
		 bufHdr->tag.rnode,
		 bufHdr->tag.blockNum,
		 BUF_STATE_GET_REFCOUNT(buf_state),
		 BUF_STATE_GET_USAGECOUNT(buf_state));
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
