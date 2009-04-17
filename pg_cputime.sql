-- Copyright (C) 2008 Mark Wong
CREATE OR REPLACE FUNCTION pg_cputime(
		OUT "user" BIGINT,
		OUT nice BIGINT,
		OUT system BIGINT,
		OUT idle BIGINT,
		OUT iowait BIGINT)
RETURNS SETOF record
AS '$libdir/pg_cputime', 'pg_cputime'
LANGUAGE C IMMUTABLE STRICT;
