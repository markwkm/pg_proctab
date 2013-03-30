EXTENSION = pg_proctab
EXTVERSION := $(shell grep default_version $(EXTENSION).control | \
		sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

DATA := $(filter-out $(wildcard sql/*--*.sql),$(wildcard sql/*.sql))
DOCS := $(wildcard doc/*)
MODULES := $(patsubst %.c,%,$(wildcard src/*.c))
PG_CONFIG = pg_config
PG91 := $(shell $(PG_CONFIG) --version | grep -qE " 8\.| 9\.0" && echo no || echo yes)

ifeq ($(PG91),yes)
all: sql/$(EXTENSION)--$(EXTVERSION).sql

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

DATA = $(wildcard sql/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql
endif

# Determine the database version in order to build with the right queries.

PG_VERSION := $(shell $(PG_CONFIG) --version | sed -n 's/PostgreSQL \([0-9]*\.[0-9]*\).*/\1/p')
PG_VERSION_LT92 = $(shell expr $(PG_VERSION) '<' 9.2)

ifeq ($(PG_VERSION_LT92),1)
PG_CPPFLAGS = -DPG91=1
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
