CMAKE_MINIMUM_REQUIRED(VERSION 2.4)
PROJECT(pg_proctab)

#
# Files to build.
#

ADD_LIBRARY(pg_cputime SHARED pg_cputime.c)
ADD_LIBRARY(pg_loadavg SHARED pg_loadavg.c)
ADD_LIBRARY(pg_memusage SHARED pg_memusage.c)
ADD_LIBRARY(pg_proctab SHARED pg_proctab.c)

#
# Set variables from pg_config data.
#

FIND_PROGRAM(PG_CONFIG pg_config)

EXEC_PROGRAM(${PG_CONFIG} ARGS --includedir OUTPUT_VARIABLE PG_INCLUDEDIR)
EXEC_PROGRAM(${PG_CONFIG} ARGS --includedir-server
		OUTPUT_VARIABLE PG_INCLUDEDIR_SERVER)
SET(PGFLAGS "-I${PG_INCLUDEDIR} -I${PG_INCLUDEDIR_SERVER}")

EXEC_PROGRAM(${PG_CONFIG} ARGS --pkglibdir OUTPUT_VARIABLE PKGLIBDIR)
EXEC_PROGRAM(${PG_CONFIG} ARGS --sharedir OUTPUT_VARIABLE SHAREDIR)

#
# No need for .so prefix.
#

SET_TARGET_PROPERTIES(pg_cputime pg_loadavg pg_memusage pg_proctab PROPERTIES
		PREFIX "")

#
# Set compiler flags.
#

SET_SOURCE_FILES_PROPERTIES(pg_cputime.c pg_loadavg.c pg_memusage.c
		pg_proctab.c COMPILE_FLAGS "-Wall ${PGFLAGS}")

#
# Set linker flags based on platform.
#

IF (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
	SET_TARGET_PROPERTIES(pg_cputime pg_loadavg pg_memusage pg_proctab
			PROPERTIES LINK_FLAGS -lkstat)
ENDIF (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")

#
# Install rules.
#

SET(CMAKE_INSTALL_PREFIX "")
INSTALL(FILES pg_cputime.sql pg_loadavg.sql pg_memusage.sql pg_proctab.sql
		DESTINATION "${SHAREDIR}/contrib")
INSTALL(FILES pg_cputime.so pg_loadavg.so pg_memusage.so pg_proctab.so
		DESTINATION ${PKGLIBDIR})
