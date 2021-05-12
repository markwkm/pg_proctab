DROP FUNCTION pg_diskusage();
CREATE FUNCTION pg_diskusage (
        OUT major smallint,
        OUT minor smallint,
        OUT devname text,
        OUT reads_completed bigint,
        OUT reads_merged bigint,
        OUT sectors_read bigint,
        OUT readtime bigint,
        OUT writes_completed bigint,
        OUT writes_merged bigint,
        OUT sectors_written bigint,
        OUT writetime bigint,
        OUT current_io bigint,
        OUT iotime bigint,
        OUT totaliotime bigint,
	OUT discards_completed bigint,
	OUT discards_merged bigint,
	OUT sectors_discarded bigint,
	OUT discardtime bigint,
	OUT flushes_completed bigint,
	OUT flushtime bigint
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_diskusage'
LANGUAGE C IMMUTABLE STRICT;
