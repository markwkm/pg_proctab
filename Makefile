MODULES = pg_cputime pg_loadavg pg_memusage pg_proctab
DATA_built = pg_cputime.sql pg_loadavg.sql pg_memusage.sql pg_proctab.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
