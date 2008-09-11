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

#ifdef __linux__
static inline char *skip_token(const char *);

static inline char *
skip_token(const char *p)
{
	while (isspace(*p))
		p++;
	while (*p && !isspace(*p))
		p++;
	return (char *) p;
}

#endif /* __linux__ */

Datum pg_memusage(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_memusage);

Datum pg_memusage(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

#ifdef __linux__
	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;
#endif /* __linux__ */

	enum loadavg {i_memused, i_memfree, i_memshared, i_membuffers,
			i_memcached, i_swapused, i_swapfree, i_swapcached};

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

		/* Check if /proc is mounted. */
		if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
		{
			elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
			SRF_RETURN_DONE(funcctx);
		} 
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

		int length;
		unsigned long memfree = 0;
		unsigned long memtotal = 0;
		unsigned long swapfree = 0;
		unsigned long swaptotal = 0;

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

#ifdef __linux__
		sprintf(buffer, "%s/meminfo", PROCFS);
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(ERROR, "'%s' not found", buffer);
			SRF_RETURN_DONE(funcctx);
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
				p = skip_token(p);

				while (p[0] == ' ')
					++p;
				GET_NEXT_VALUE(p, q, values[i_membuffers], length,
						"Buffers not found", ' ');
				elog(DEBUG5, "pg_memusage: Buffers = %s",
						values[i_membuffers]);
			}
			else if (strncmp(p, "Cached:", 7) == 0)
			{
				p = skip_token(p);

				while (p[0] == ' ')
					++p;
				GET_NEXT_VALUE(p, q, values[i_memcached], length,
						"Cached not found", ' ');
				elog(DEBUG5, "pg_memusage: Cached = %s",
						values[i_memcached]);
			}
			else if (strncmp(p, "MemFree:", 8) == 0)
			{
				p = skip_token(p);

				memfree = strtoul(p, &p, 10);
				elog(DEBUG5, "pg_memusage: MemFree = %lu", memfree);
				sprintf(values[i_memused], "%lu", memtotal - memfree);
				sprintf(values[i_memfree], "%lu", memfree);
			}
			else if (strncmp(p, "MemShared:", 10) == 0)
			{
				p = skip_token(p);

				while (p[0] == ' ')
					++p;
				GET_NEXT_VALUE(p, q, values[i_memshared], length,
						"MemShared not found", ' ');
				elog(DEBUG5, "pg_memusage: MemShared = %s",
						values[i_memshared]);
			}
			else if (strncmp(p, "MemTotal:", 9) == 0)
			{
				p = skip_token(p);

				memtotal = strtoul(p, &p, 10);
				elog(DEBUG5, "pg_memusage: MemTotal = %lu", memtotal);
			}
			else if (strncmp(p, "SwapFree:", 9) == 0)
			{
				p = skip_token(p);

				swapfree = strtoul(p, &p, 10);
				elog(DEBUG5, "pg_memusage: SwapFree = %lu", swapfree);
				sprintf(values[i_swapused], "%lu", swaptotal - swapfree);
				sprintf(values[i_swapfree], "%lu", swapfree);
			}
			else if (strncmp(p, "SwapCached:", 11) == 0)
			{
				p = skip_token(p);

				while (p[0] == ' ')
					++p;
				GET_NEXT_VALUE(p, q, values[i_swapcached], length,
						"SwapCached not found", ' ');
				elog(DEBUG5, "pg_memusage: SwapCached = %s",
						values[i_swapcached]);
			}
			else if (strncmp(p, "SwapTotal:", 10) == 0)
			{
				p = skip_token(p);

				swaptotal = strtoul(p, &p, 10);
				elog(DEBUG5, "pg_memusage: SwapTotal = %lu", swaptotal);
			}
			p = strchr(p, '\n');
		}
#endif /* __linux__ */

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
