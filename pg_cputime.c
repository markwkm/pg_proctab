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

enum loadavg {i_user, i_nice, i_system, i_idle, i_iowait};

int get_cputime(char **);
Datum pg_cputime(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_cputime);

Datum pg_cputime(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

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

		values = (char **) palloc(5 * sizeof(char *));
		values[i_user] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_nice] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_system] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_idle] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_iowait] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		if (get_cputime(values) == 0)
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
get_cputime(char **values)
{
#ifdef __linux__
	struct statfs sb;
	int fd;
	int len;
	char buffer[4096];
	char *p;
	char *q;

	int length;

	/* Check if /proc is mounted. */
	if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
	{
		elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
		return 0;
	}

	snprintf(buffer, sizeof(buffer) - 1, "%s/stat", PROCFS);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_cputime: %s", buffer);

	p = buffer;

	SKIP_TOKEN(p);			/* skip cpu */

	/* user */
	GET_NEXT_VALUE(p, q, values[i_user], length, "user not found", ' ');

	/* nice */
	GET_NEXT_VALUE(p, q, values[i_nice], length, "nice not found", ' ');

	/* system */
	GET_NEXT_VALUE(p, q, values[i_system], length, "system not found", ' ');

	/* idle */
	GET_NEXT_VALUE(p, q, values[i_idle], length, "idle not found", ' ');

	/* iowait */
	GET_NEXT_VALUE(p, q, values[i_iowait], length, "iowait not found", ' ');
#endif /* __linux__ */

	elog(DEBUG5, "pg_cputime: [%d] user = %s", (int) i_user, values[i_user]);
	elog(DEBUG5, "pg_cputime: [%d] nice = %s", (int) i_nice, values[i_nice]);
	elog(DEBUG5, "pg_cputime: [%d] system = %s", (int) i_system,
			values[i_system]);
	elog(DEBUG5, "pg_cputime: [%d] idle = %s", (int) i_idle, values[i_idle]);
	elog(DEBUG5, "pg_cputime: [%d] iowait = %s", (int) i_iowait,
			values[i_iowait]);

	return 1;
}
