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

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define BIGINT_LEN 20
#define INTEGER_LEN 10

#ifdef __linux__
#if 0
#include <linux/proc_fs.h>
#else
#define PROC_SUPER_MAGIC 0x9fa0
#endif

#define PROCFS "/proc"

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

Datum pg_cputime(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_cputime);

Datum pg_cputime(PG_FUNCTION_ARGS)
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

	enum loadavg {i_user, i_nice, i_system, i_idle, i_iowait};

	elog(DEBUG5, "pg_cputime: Entering stored function.");

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

		values = (char **) palloc(5 * sizeof(char *));
		values[i_user] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_nice] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_system] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_idle] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_iowait] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));

#ifdef __linux__
		sprintf(buffer, "%s/stat", PROCFS);
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(ERROR, "'%s' not found", buffer);
			SRF_RETURN_DONE(funcctx);
		}
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		elog(DEBUG5, "pg_cputime: %s", buffer);

		p = buffer;

		p = skip_token(p);			/* skip cpu */
		++p;
		++p;

		/* user */
		GET_NEXT_VALUE(p, q, values[i_user], length, "user not found", ' ');
		elog(DEBUG5, "pg_cputime: user = %s", values[i_user]);

		/* nice */
		GET_NEXT_VALUE(p, q, values[i_nice], length, "nice not found", ' ');
		elog(DEBUG5, "pg_cputime: nice = %s", values[i_nice]);

		/* system */
		GET_NEXT_VALUE(p, q, values[i_system], length, "system not found",
				' ');
		elog(DEBUG5, "pg_cputime: system = %s", values[i_system]);

		/* idle */
		GET_NEXT_VALUE(p, q, values[i_idle], length, "idle not found", ' ');
		elog(DEBUG5, "pg_cputime: idle = %s", values[i_idle]);

		/* iowait */
		GET_NEXT_VALUE(p, q, values[i_iowait], length, "iowait not found",
				' ');
		elog(DEBUG5, "pg_cputime: iowait = %s", values[i_iowait]);
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
