MODULES = pg_proctab
DATA_built = pg_proctab.sql

PG_CONFIG=pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
