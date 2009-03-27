/*
 * Copyright (C) 2008 Mark Wong
 */

#include "postgres.h"
#include <string.h>
#include "fmgr.h"
#include "funcapi.h"
#include <sys/vfs.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <executor/spi.h>
#include "pg_common.h"

enum loadavg {i_memused, i_memfree, i_memshared, i_membuffers, i_memcached,
		i_swapused, i_swapfree, i_swapcached};

int get_memusage(char **);

Datum pg_memusage(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_memusage);

Datum pg_memusage(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;


	elog(DEBUG5, "pg_memusage: Entering stored function.");

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* Build a tuple descriptor for our result type */
		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("function returning record called in context "
							"that cannot accept type record")));

		/*
		 * generate attribute metadata needed later to produce tuples from raw
		 * C strings
		 */
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		funcctx->max_calls = 1;

		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (call_cntr < max_calls) /* do when there is more left to send */
	{
		HeapTuple tuple;
		Datum result;

		char **values = NULL;

		values = (char **) palloc(8 * sizeof(char *));
		values[i_memused] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memfree] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memshared] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_membuffers] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_memcached] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapused] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapfree] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_swapcached] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		if (get_memusage(values) == 0)
			SRF_RETURN_DONE(funcctx);

		/* build a tuple */
		tuple = BuildTupleFromCStrings(attinmeta, values);

		/* make the tuple into a datum */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else /* do when there is no more left */
	{
		SRF_RETURN_DONE(funcctx);
	}
}

int
get_memusage(char **values)
{
#ifdef __linux__
	int length;
	unsigned long memfree = 0;
	unsigned long memtotal = 0;
	unsigned long swapfree = 0;
	unsigned long swaptotal = 0;

	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/meminfo", PROCFS);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_memusage: %s", buffer);

	p = buffer - 1;

	values[i_memshared][0] = '0';
	values[i_memshared][1] = '\0';

	while (p != NULL) {
		++p;
		if (strncmp(p, "Buffers:", 8) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_membuffers], length,
					"Buffers not found", ' ');
		}
		else if (strncmp(p, "Cached:", 7) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_memcached], length,
					"Cached not found", ' ');
		}
		else if (strncmp(p, "MemFree:", 8) == 0)
		{
			SKIP_TOKEN(p);
			memfree = strtoul(p, &p, 10);
			snprintf(values[i_memused], BIGINT_LEN, "%lu", memtotal - memfree);
			snprintf(values[i_memfree], BIGINT_LEN, "%lu", memfree);
		}
		else if (strncmp(p, "MemShared:", 10) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_memshared], length,
					"MemShared not found", ' ');
		}
		else if (strncmp(p, "MemTotal:", 9) == 0)
		{
			SKIP_TOKEN(p);
			memtotal = strtoul(p, &p, 10);
			elog(DEBUG5, "pg_memusage: MemTotal = %lu", memtotal);
		}
		else if (strncmp(p, "SwapFree:", 9) == 0)
		{
			SKIP_TOKEN(p);
			swapfree = strtoul(p, &p, 10);
			snprintf(values[i_swapused], BIGINT_LEN, "%lu",
					swaptotal - swapfree);
			snprintf(values[i_swapfree], BIGINT_LEN, "%lu", swapfree);
		}
		else if (strncmp(p, "SwapCached:", 11) == 0)
		{
			SKIP_TOKEN(p);
			GET_NEXT_VALUE(p, q, values[i_swapcached], length,
					"SwapCached not found", ' ');
		}
		else if (strncmp(p, "SwapTotal:", 10) == 0)
		{
			SKIP_TOKEN(p);
			swaptotal = strtoul(p, &p, 10);
			elog(DEBUG5, "pg_memusage: SwapTotal = %lu", swaptotal);
		}
		p = strchr(p, '\n');
	}
#endif /* __linux__ */

	elog(DEBUG5, "pg_memusage: [%d] Buffers = %s", (int) i_membuffers,
			values[i_membuffers]);
	elog(DEBUG5, "pg_memusage: [%d] Cached = %s", (int) i_memcached,
			values[i_memcached]);
	elog(DEBUG5, "pg_memusage: [%d] MemFree = %s", (int) i_memfree,
			values[i_memfree]);
	elog(DEBUG5, "pg_memusage: [%d] MemUsed = %s", (int) i_memused,
			values[i_memused]);
	elog(DEBUG5, "pg_memusage: [%d] MemShared = %s", (int) i_memshared,
			values[i_memshared]);
	elog(DEBUG5, "pg_memusage: [%d] SwapCached = %s", (int) i_swapcached,
			values[i_swapcached]);
	elog(DEBUG5, "pg_memusage: [%d] SwapFree = %s", (int) i_swapfree,
			values[i_swapfree]);
	elog(DEBUG5, "pg_memusage: [%d] SwapUsed = %s", (int) i_swapused,
			values[i_swapused]);

	return 1;
}
