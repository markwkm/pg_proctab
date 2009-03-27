-- Copyright (C) 2008 Mark Wong
CREATE OR REPLACE FUNCTION pg_memusage(
		OUT memused BIGINT,
		OUT memfree BIGINT,
		OUT memshared BIGINT,
		OUT membuffers BIGINT,
		OUT memcached BIGINT,
		OUT swapused BIGINT,
		OUT swapfree BIGINT,
		OUT swapcached BIGINT)
RETURNS SETOF record
AS '$libdir/pg_memusage', 'pg_memusage'
LANGUAGE C IMMUTABLE STRICT;
