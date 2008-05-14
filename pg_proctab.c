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

#if 0
#include <linux/proc_fs.h>
#else
#define PROC_SUPER_MAGIC 0x9fa0
#endif

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define PROCFS "/proc"

#define INTEGER_LEN 10
#define BIGINT_LEN 20

#define GET_NEXT_VALUE(p, q, value, length, msg) \
		if ((q = strchr(p, ' ')) == NULL) \
		{ \
			elog(ERROR, msg); \
			SRF_RETURN_DONE(funcctx); \
		} \
		length = q - p; \
		strncpy(value, p, length); \
		value[length] = '\0'; \
elog(WARNING, msg ": '%s'", value); \
		p = q + 1;

Datum pg_proctab(PG_FUNCTION_ARGS);
static inline char *skip_token(const char *);

PG_FUNCTION_INFO_V1(pg_proctab);

Datum pg_proctab(PG_FUNCTION_ARGS)
{
	int32 pid = PG_GETARG_INT32(0);

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

	enum proctab {i_pid, i_comm, i_state, i_ppid, i_pgrp, i_session,
			i_tty_nr, i_tpgid, i_flags, i_minflt, i_cminflt, i_majflt,
			i_cmajflt, i_utime, i_stime, i_cutime, i_cstime, i_priority,
			i_nice, i_num_threads, i_itrealvalue, i_starttime, i_vsize,
			i_rss, i_signal, i_blocked, i_sigignore, i_sigcatch,
			i_wchan, i_exit_signal, i_processor, i_rt_priority, i_policy,
			i_delayacct_blkio_ticks};
	char **values = NULL;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		values = (char **) palloc(35 * sizeof(char *));
		values[i_pid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_comm] = (char *) palloc(128 * sizeof(char));
		values[i_state] = (char *) palloc(2 * sizeof(char));
		values[i_ppid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_pgrp] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_session] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_tty_nr] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_tpgid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_flags] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_minflt] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_cminflt] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_majflt] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_cmajflt] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_utime] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_stime] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_cutime] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_cstime] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_priority] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_nice] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_num_threads] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_itrealvalue] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_starttime] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_vsize] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_rss] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_signal] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_blocked] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_sigignore] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_sigcatch] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_wchan] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_exit_signal] =
				(char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_processor] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_rt_priority] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_policy] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_delayacct_blkio_ticks] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* total number of tuples to be returned */
		funcctx->max_calls = 1;

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

		/* Check if /proc is mounted. */
		if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
		{
			elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
			SRF_RETURN_DONE(funcctx);
		} 
		chdir(PROCFS);

		/* Read the stat info for the pid. */

		/*
		 * Sanity check, make sure we read the pid information that we're
		 * asking for.
		 */ 
		sprintf(buffer, "%d/stat", pid);
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(WARNING, "%d/stat not found", pid);
			SRF_RETURN_DONE(funcctx);
		}
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';

		p = buffer;

		/* pid */
		GET_NEXT_VALUE(p, q, values[i_pid], length, "pid not found");

		/* comm */
		++p;
		if ((q = strchr(p, ')')) == NULL)
		{
			elog(ERROR, "comm not found");
			SRF_RETURN_DONE(funcctx);
		}
		length = q - p;
		strncpy(values[i_comm], p, length);
		values[i_comm][length] = '\0';
		p = q + 2;

		/* state */
		values[i_state][0] = *p;
		values[i_state][1] = '\0';
		p = p + 2;

		/* ppid */
		GET_NEXT_VALUE(p, q, values[i_ppid], length, "ppid not found");

		/* pgrp */
		GET_NEXT_VALUE(p, q, values[i_pgrp], length, "pgrp not found");

		/* session */
		GET_NEXT_VALUE(p, q, values[i_session], length, "session not found");

		/* tty_nr */
		GET_NEXT_VALUE(p, q, values[i_tty_nr], length, "tty_nr not found");

		/* tpgid */
		GET_NEXT_VALUE(p, q, values[i_tpgid], length, "tpgid not found");

		/* flags */
		GET_NEXT_VALUE(p, q, values[i_flags], length, "flags not found");

		/* minflt */
		GET_NEXT_VALUE(p, q, values[i_minflt], length, "minflt not found");

		/* cminflt */
		GET_NEXT_VALUE(p, q, values[i_cminflt], length, "cminflt not found");

		/* majflt */
		GET_NEXT_VALUE(p, q, values[i_majflt], length, "majflt not found");

		/* cmajflt */
		GET_NEXT_VALUE(p, q, values[i_cmajflt], length, "cmajflt not found");

		/* utime */
		GET_NEXT_VALUE(p, q, values[i_utime], length, "utime not found");

		/* stime */
		GET_NEXT_VALUE(p, q, values[i_stime], length, "stime not found");

		/* cutime */
		GET_NEXT_VALUE(p, q, values[i_cutime], length, "cutime not found");

		/* cstime */
		GET_NEXT_VALUE(p, q, values[i_cstime], length, "cstime not found");

		/* priority */
		GET_NEXT_VALUE(p, q, values[i_priority], length, "priority not found");

		/* nice */
		GET_NEXT_VALUE(p, q, values[i_nice], length, "nice not found");

		/* num_threads */
		GET_NEXT_VALUE(p, q, values[i_num_threads], length,
				"num_threads not found");

		/* itrealvalue */
		GET_NEXT_VALUE(p, q, values[i_itrealvalue], length,
				"itrealvalue not found");

		/* starttime */
		GET_NEXT_VALUE(p, q, values[i_starttime], length,
				"starttime not found");

		/* vsize */
		GET_NEXT_VALUE(p, q, values[i_vsize], length, "vsize not found");

		/* rss */
		GET_NEXT_VALUE(p, q, values[i_rss], length, "rss not found");

		++p;
		p = skip_token(p);			/* skip rlim */
		p = skip_token(p);			/* skip startcode */
		p = skip_token(p);			/* skip endcode */
		p = skip_token(p);			/* skip startstack */
		p = skip_token(p);			/* skip kstkesp */
		p = skip_token(p);			/* skip kstkeip */
		++p;

		/* signal */
		GET_NEXT_VALUE(p, q, values[i_signal], length, "signal not found");

		/* blocked */
		GET_NEXT_VALUE(p, q, values[i_blocked], length, "blocked not found");

		/* sigignore */
		GET_NEXT_VALUE(p, q, values[i_sigignore], length,
				"sigignore not found");

		/* sigcatch */
		GET_NEXT_VALUE(p, q, values[i_sigcatch], length, "sigcatch not found");

		/* wchan */
		GET_NEXT_VALUE(p, q, values[i_wchan], length, "wchan not found");

		++p;
		p = skip_token(p);			/* skip nswap */
		p = skip_token(p);			/* skip cnswap */
		++p;

		/* exit_signal */
		GET_NEXT_VALUE(p, q, values[i_exit_signal], length,
				"exit_signal not found");

		/* processor */
		GET_NEXT_VALUE(p, q, values[i_processor], length,
				"processor not found");

		/* rt_priority */
		GET_NEXT_VALUE(p, q, values[i_rt_priority], length,
				"rt_priority not found");

		/* policy */
		GET_NEXT_VALUE(p, q, values[i_policy], length, "policy not found");

		/* delayacct_blkio_ticks */
		GET_NEXT_VALUE(p, q, values[i_delayacct_blkio_ticks], length,
				"delayacct_blkio_ticks not found");

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
