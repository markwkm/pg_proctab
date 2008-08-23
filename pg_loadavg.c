/*
 * Copyright (C) 2008 Mark Wong
 *
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

#if 0
#include <linux/proc_fs.h>
#else
#define PROC_SUPER_MAGIC 0x9fa0
#endif

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define PROCFS "/proc"

#define FLOAT_LEN 20
#define INTEGER_LEN 10

#define GET_NEXT_VALUE(p, q, value, length, msg, delim) \
		if ((q = strchr(p, delim)) == NULL) \
		{ \
			elog(ERROR, msg); \
			SRF_RETURN_DONE(funcctx); \
		} \
		length = q - p; \
		strncpy(value, p, length); \
		value[length] = '\0'; \
		p = q + 1;

Datum pg_loadavg(PG_FUNCTION_ARGS);
static inline char *skip_token(const char *);

PG_FUNCTION_INFO_V1(pg_loadavg);

Datum pg_loadavg(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

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

		chdir(PROCFS);

		/*
		 * Sanity check, make sure we read the pid information that we're
		 * asking for.
		 */ 
		sprintf(buffer, "loadavg");
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(ERROR, "loadavg not found");
			SRF_RETURN_DONE(funcctx);
		}
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		elog(DEBUG5, "pg_loadavg: %s", buffer);

		values = (char **) palloc(4 * sizeof(char *));
		values[i_load1] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load5] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_load15] = (char *) palloc((FLOAT_LEN + 1) * sizeof(char));
		values[i_last_pid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));

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

static inline char *
skip_token(const char *p)
{
	while (isspace(*p))
		p++;
	while (*p && !isspace(*p))
		p++;
	return (char *) p;
}
