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

CREATE FUNCTION pg_lockforextension(
regclass)
RETURNS INT
AS 'MODULE_PATHNAME', 'pg_lockforextension'
LANGUAGE C STRICT;

CREATE FUNCTION multi_exec(
nworkers int,
relid regclass
)
RETURNS INT
AS 'MODULE_PATHNAME', 'multi_exec'
LANGUAGE C STRICT;

CREATE FUNCTION replock()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'replock'
LANGUAGE C STRICT;

CREATE FUNCTION show_define_variables()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'show_define_variables'
LANGUAGE C STRICT;
