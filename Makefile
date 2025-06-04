EXTENSION = pg_proctab
EXTVERSION := $(shell grep default_version $(EXTENSION).control | \
		sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

DATA := $(filter-out $(wildcard sql/*--*.sql),$(wildcard sql/*.sql),$(wildcard contrib/*.sql))
DOCS := $(wildcard doc/*)
MODULES := $(patsubst %.c,%,$(wildcard src/*.c))
SCRIPTS := $(wildcard contrib/*.sh) $(wildcard contrib/*.pl)

ifdef USE_PGXS
PG_CONFIG = pg_config
PG91 := $(shell $(PG_CONFIG) --version | grep -qE " 8\.| 9\.0" && echo no || echo yes)
ifeq ($(PG91),yes)
all: sql/$(EXTENSION)--$(EXTVERSION).sql

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

DATA = $(wildcard sql/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_proctab
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
