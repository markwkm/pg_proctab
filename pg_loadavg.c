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

Datum pg_loadavg(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_loadavg);

Datum pg_loadavg(PG_FUNCTION_ARGS)
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

	enum loadavg {i_load1, i_load5, i_load15, i_last_pid};

	elog(DEBUG5, "pg_loadavg: Entering stored function.");

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

		char **values = NULL;

		values = (char **) palloc(4 * sizeof(char *));
		values[i_load1] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load5] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load15] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_last_pid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));

#ifdef __linux__
		sprintf(buffer, "%s/loadavg", PROCFS);
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(ERROR, "'%s' not found", buffer);
			SRF_RETURN_DONE(funcctx);
		}
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		elog(DEBUG5, "pg_loadavg: %s", buffer);

		p = buffer;

		/* load1 */
		GET_NEXT_VALUE(p, q, values[i_load1], length, "load1 not found", ' ');
		elog(DEBUG5, "pg_loadavg: load1 = %s", values[i_load1]);

		/* load5 */
		GET_NEXT_VALUE(p, q, values[i_load5], length, "load5 not found", ' ');
		elog(DEBUG5, "pg_loadavg: load5 = %s", values[i_load5]);

		/* load15 */
		GET_NEXT_VALUE(p, q, values[i_load15], length, "load15 not found",
				' ');
		elog(DEBUG5, "pg_loadavg: load15 = %s", values[i_load15]);

		p = skip_token(p);			/* skip running/tasks */
		++p;

		/* last_pid */
		/*
		 * It appears sometimes this is the last item in /proc/PID/stat and
		 * sometimes it's not, depending on the version of the kernel and
		 * possibly the architecture.  So first test if it is the last item
		 * before determining how to deliminate it.
		 */
		if (strchr(p, ' ') == NULL)
		{
			GET_NEXT_VALUE(p, q, values[i_last_pid], length,
					"last_pid not found", '\n');
		}
		else
		{
			GET_NEXT_VALUE(p, q, values[i_last_pid], length,
					"last_pid not found", ' ');
		}
		elog(DEBUG5, "pg_loadavg: last_pid = %s",
				values[i_last_pid]);
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
