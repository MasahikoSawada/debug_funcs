/* debug_funcs--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION debug_funcs" to load this file. \quit

CREATE FUNCTION pg_LockBufferForCleanup(
regclass,
blkno bigint,
sleep int)
RETURNS INT
AS 'MODULE_PATHNAME', 'pg_LockBufferForCleanup'
LANGUAGE C STRICT;

CREATE FUNCTION pg_LockBuffer(
regclass,
blkno bigint,
mode text,
sleep int)
RETURNS INT
AS 'MODULE_PATHNAME', 'pg_LockBuffer'
LANGUAGE C STRICT;
