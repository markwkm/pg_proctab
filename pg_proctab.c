/*
 * Copyright (C) 2008 Mark Wong
 */

#include "postgres.h"
#include <string.h>
#include "fmgr.h"
#include "funcapi.h"
#include <sys/vfs.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <executor/spi.h>

#ifdef __linux__
#include <ctype.h>

/*
 * For details on the Linux process table, see the description of
 * /proc/PID/stat in Documentation/filesystems/proc.txt in the Linux source
 * code.
 */

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

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define INTEGER_LEN 10
#define BIGINT_LEN 20

#define GET_PIDS \
		"SELECT procpid " \
		"FROM pg_stat_activity"

Datum pg_proctab(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_proctab);

Datum pg_proctab(PG_FUNCTION_ARGS)
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

	enum proctab {i_pid, i_comm, i_state, i_ppid, i_pgrp, i_session,
			i_tty_nr, i_tpgid, i_flags, i_minflt, i_cminflt, i_majflt,
			i_cmajflt, i_utime, i_stime, i_cutime, i_cstime, i_priority,
			i_nice, i_num_threads, i_itrealvalue, i_starttime, i_vsize,
			i_rss, i_exit_signal, i_processor, i_rt_priority, i_policy,
			i_delayacct_blkio_ticks};

	elog(DEBUG5, "pg_proctab: Entering stored function.");

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		int ret;

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

		/* Get pid of all client connections. */

		SPI_connect();
		elog(DEBUG5, "pg_proctab: SPI connected.");

		ret = SPI_exec(GET_PIDS, 0);
		if (ret == SPI_OK_SELECT)
		{
			int32 *ppid;

			int i;
			TupleDesc tupdesc;
			SPITupleTable *tuptable;
			HeapTuple tuple;

			/* total number of tuples to be returned */
			funcctx->max_calls = SPI_processed;
			elog(DEBUG5, "pg_proctab: %d process(es) in pg_stat_activity.",
					funcctx->max_calls);
			funcctx->user_fctx = MemoryContextAlloc(
					funcctx->multi_call_memory_ctx, sizeof(int32) *
					funcctx->max_calls);
			ppid = (int32 *) funcctx->user_fctx;

			tupdesc = SPI_tuptable->tupdesc;
			tuptable = SPI_tuptable;

			for (i = 0; i < funcctx->max_calls; i++)
			{
				tuple = tuptable->vals[i];
				ppid[i] = atoi(SPI_getvalue(tuple, tupdesc, 1));
				elog(DEBUG5, "pg_proctab: saving pid %d.", ppid[i]);
			}
		}
		else
		{
			/* total number of tuples to be returned */
			funcctx->max_calls = 0;
			elog(WARNING, "unable to get procpids from pg_stat_activity");
		}

		SPI_finish();

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

		int32 *ppid;
		int32 pid;
		int length;

		char **values = NULL;

#ifdef __linux__
		/* Check if /proc is mounted. */
		if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
		{
			elog(ERROR, "proc filesystem not mounted on " PROCFS "\n");
			SRF_RETURN_DONE(funcctx);
		} 
		chdir(PROCFS);

		/* Read the stat info for the pid. */

		ppid = (int32 *) funcctx->user_fctx;
		pid = ppid[call_cntr];
		elog(DEBUG5, "pg_proctab: accessing process table for pid[%d] %d.",
				call_cntr, pid);

		/*
		 * Sanity check, make sure we read the pid information that we're
		 * asking for.
		 */ 
		sprintf(buffer, "%d/stat", pid);
		fd = open(buffer, O_RDONLY);
		if (fd == -1)
		{
			elog(ERROR, "%d/stat not found", pid);
			SRF_RETURN_DONE(funcctx);
		}
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		elog(DEBUG5, "pg_proctab: %s", buffer);

		values = (char **) palloc(30 * sizeof(char *));
		values[i_pid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_comm] = (char *) palloc(1024 * sizeof(char));
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
		values[i_exit_signal] =
				(char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_processor] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_rt_priority] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_policy] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_delayacct_blkio_ticks] =
				(char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		p = buffer;

		/* pid */
		GET_NEXT_VALUE(p, q, values[i_pid], length, "pid not found", ' ');
		elog(DEBUG5, "pg_proctab: pid = %s", values[i_pid]);

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
		elog(DEBUG5, "pg_proctab: comm = %s", values[i_comm]);

		/* state */
		values[i_state][0] = *p;
		values[i_state][1] = '\0';
		p = p + 2;
		elog(DEBUG5, "pg_proctab: state = %s", values[i_state]);

		/* ppid */
		GET_NEXT_VALUE(p, q, values[i_ppid], length, "ppid not found", ' ');
		elog(DEBUG5, "pg_proctab: ppid = %s", values[i_ppid]);

		/* pgrp */
		GET_NEXT_VALUE(p, q, values[i_pgrp], length, "pgrp not found", ' ');
		elog(DEBUG5, "pg_proctab: pgrp = %s", values[i_pgrp]);

		/* session */
		GET_NEXT_VALUE(p, q, values[i_session], length, "session not found",
				' ');
		elog(DEBUG5, "pg_proctab: session = %s", values[i_session]);

		/* tty_nr */
		GET_NEXT_VALUE(p, q, values[i_tty_nr], length, "tty_nr not found", ' ');
		elog(DEBUG5, "pg_proctab: tty_nr = %s", values[i_tty_nr]);

		/* tpgid */
		GET_NEXT_VALUE(p, q, values[i_tpgid], length, "tpgid not found", ' ');
		elog(DEBUG5, "pg_proctab: tpgid = %s", values[i_tpgid]);

		/* flags */
		GET_NEXT_VALUE(p, q, values[i_flags], length, "flags not found", ' ');
		elog(DEBUG5, "pg_proctab: flags = %s", values[i_flags]);

		/* minflt */
		GET_NEXT_VALUE(p, q, values[i_minflt], length, "minflt not found", ' ');
		elog(DEBUG5, "pg_proctab: minflt = %s", values[i_minflt]);

		/* cminflt */
		GET_NEXT_VALUE(p, q, values[i_cminflt], length, "cminflt not found",
				' ');
		elog(DEBUG5, "pg_proctab: cminflt = %s", values[i_cminflt]);

		/* majflt */
		GET_NEXT_VALUE(p, q, values[i_majflt], length, "majflt not found", ' ');
		elog(DEBUG5, "pg_proctab: majflt = %s", values[i_majflt]);

		/* cmajflt */
		GET_NEXT_VALUE(p, q, values[i_cmajflt], length, "cmajflt not found",
				' ');
		elog(DEBUG5, "pg_proctab: cmajflt = %s", values[i_cmajflt]);

		/* utime */
		GET_NEXT_VALUE(p, q, values[i_utime], length, "utime not found", ' ');
		elog(DEBUG5, "pg_proctab: utime = %s", values[i_utime]);

		/* stime */
		GET_NEXT_VALUE(p, q, values[i_stime], length, "stime not found", ' ');
		elog(DEBUG5, "pg_proctab: stime = %s", values[i_stime]);

		/* cutime */
		GET_NEXT_VALUE(p, q, values[i_cutime], length, "cutime not found", ' ');
		elog(DEBUG5, "pg_proctab: cutime = %s", values[i_cutime]);

		/* cstime */
		GET_NEXT_VALUE(p, q, values[i_cstime], length, "cstime not found", ' ');
		elog(DEBUG5, "pg_proctab: cstime = %s", values[i_cstime]);

		/* priority */
		GET_NEXT_VALUE(p, q, values[i_priority], length, "priority not found",
				' ');
		elog(DEBUG5, "pg_proctab: priority = %s", values[i_priority]);

		/* nice */
		GET_NEXT_VALUE(p, q, values[i_nice], length, "nice not found", ' ');
		elog(DEBUG5, "pg_proctab: nice = %s", values[i_nice]);

		/* num_threads */
		GET_NEXT_VALUE(p, q, values[i_num_threads], length,
				"num_threads not found", ' ');
		elog(DEBUG5, "pg_proctab: num_threads = %s", values[i_num_threads]);

		/* itrealvalue */
		GET_NEXT_VALUE(p, q, values[i_itrealvalue], length,
				"itrealvalue not found", ' ');
		elog(DEBUG5, "pg_proctab: itrealvalue = %s", values[i_itrealvalue]);

		/* starttime */
		GET_NEXT_VALUE(p, q, values[i_starttime], length,
				"starttime not found", ' ');
		elog(DEBUG5, "pg_proctab: starttime = %s", values[i_starttime]);

		/* vsize */
		GET_NEXT_VALUE(p, q, values[i_vsize], length, "vsize not found", ' ');
		elog(DEBUG5, "pg_proctab: vsize = %s", values[i_vsize]);

		/* rss */
		GET_NEXT_VALUE(p, q, values[i_rss], length, "rss not found", ' ');
		elog(DEBUG5, "pg_proctab: rss = %s", values[i_rss]);

		p = skip_token(p);			/* skip rlim */
		p = skip_token(p);			/* skip startcode */
		p = skip_token(p);			/* skip endcode */
		p = skip_token(p);			/* skip startstack */
		p = skip_token(p);			/* skip kstkesp */
		p = skip_token(p);			/* skip kstkeip */
		p = skip_token(p);			/* skip signal (obsolete) */
		p = skip_token(p);			/* skip blocked (obsolete) */
		p = skip_token(p);			/* skip sigignore (obsolete) */
		p = skip_token(p);			/* skip sigcatch (obsolete) */
		p = skip_token(p);			/* skip wchan */
		p = skip_token(p);			/* skip nswap (place holder) */
		p = skip_token(p);			/* skip cnswap (place holder) */
		++p;

		/* exit_signal */
		GET_NEXT_VALUE(p, q, values[i_exit_signal], length,
				"exit_signal not found", ' ');
		elog(DEBUG5, "pg_proctab: exit_signal = %s", values[i_exit_signal]);

		/* processor */
		GET_NEXT_VALUE(p, q, values[i_processor], length,
				"processor not found", ' ');
		elog(DEBUG5, "pg_proctab: processor = %s", values[i_processor]);

		/* rt_priority */
		GET_NEXT_VALUE(p, q, values[i_rt_priority], length,
				"rt_priority not found", ' ');
		elog(DEBUG5, "pg_proctab: rt_priority = %s", values[i_rt_priority]);

		/* policy */
		GET_NEXT_VALUE(p, q, values[i_policy], length, "policy not found", ' ');
		elog(DEBUG5, "pg_proctab: policy = %s", values[i_policy]);

		/* delayacct_blkio_ticks */
		/*
		 * It appears sometimes this is the last item in /proc/PID/stat and
		 * sometimes it's not, depending on the version of the kernel and
		 * possibly the architecture.  So first test if it is the last item
		 * before determining how to deliminate it.
		 */
		if (strchr(p, ' ') == NULL)
		{
			GET_NEXT_VALUE(p, q, values[i_delayacct_blkio_ticks], length,
					"delayacct_blkio_ticks not found", '\n');
		}
		else
		{
			GET_NEXT_VALUE(p, q, values[i_delayacct_blkio_ticks], length,
					"delayacct_blkio_ticks not found", ' ');
		}
		elog(DEBUG5, "pg_proctab: delayacct_blkio_ticks = %s",
				values[i_delayacct_blkio_ticks]);
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
