CREATE OR REPLACE FUNCTION pg_diskusage (
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
        OUT totaliotime bigint)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_diskusage'
LANGUAGE C IMMUTABLE STRICT;
