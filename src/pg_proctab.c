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
#include "pg_common.h"

#define FULLCOMM_LEN 1024

#ifdef PG91
#define GET_PIDS \
		"SELECT procpid " \
		"FROM pg_stat_activity"
#else
#define GET_PIDS \
		"SELECT pid " \
		"FROM pg_stat_activity"
#endif /* PG91 */

#ifdef __linux__
#define GET_VALUE(value) \
		p = strchr(p, ':'); \
		++p; \
		++p; \
		q = strchr(p, '\n'); \
		len = q - p; \
		if (len >= BIGINT_LEN) \
		{ \
			elog(ERROR, "value is larger than the buffer: %d\n", __LINE__); \
			return 0; \
		} \
		strncpy(value, p, len); \
		value[len] = '\0';
#endif /* __linux__ */

enum proctab {i_pid, i_comm, i_fullcomm, i_state, i_ppid, i_pgrp, i_session,
		i_tty_nr, i_tpgid, i_flags, i_minflt, i_cminflt, i_majflt, i_cmajflt,
		i_utime, i_stime, i_cutime, i_cstime, i_priority, i_nice,
		i_num_threads, i_itrealvalue, i_starttime, i_vsize, i_rss,
		i_exit_signal, i_processor, i_rt_priority, i_policy,
		i_delayacct_blkio_ticks, i_uid, i_username, i_rchar, i_wchar, i_syscr,
		i_syscw, i_reads, i_writes, i_cwrites};

int get_proctab(FuncCallContext *, char **);

Datum pg_proctab(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(pg_proctab);

Datum pg_proctab(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;

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

		char **values = NULL;

		values = (char **) palloc(39 * sizeof(char *));
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

		/* FIXME: Need to figure out correct length to hold a C double type. */
		values[i_utime] = (char *) palloc(32 * sizeof(char));
		values[i_stime] = (char *) palloc(32 * sizeof(char));

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
		values[i_uid] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));
		values[i_rchar] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_wchar] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_syscr] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_syscw] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_reads] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_writes] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));
		values[i_cwrites] = (char *) palloc((BIGINT_LEN + 1) * sizeof(char));

		if (get_proctab(funcctx, values) == 0)
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
get_proctab(FuncCallContext *funcctx, char **values)
{
#ifdef __linux__
	/*
 	* For details on the Linux process table, see the description of
 	* /proc/PID/stat in Documentation/filesystems/proc.txt in the Linux source
 	* code.
 	*/

	int32 *ppid;
	int32 pid;
	int length;

	struct stat stat_struct;

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

	/* Read the stat info for the pid. */

	ppid = (int32 *) funcctx->user_fctx;
	pid = ppid[funcctx->call_cntr];
	elog(DEBUG5, "pg_proctab: accessing process table for pid[%d] %d.",
				funcctx->call_cntr, pid);

	/* Get the full command line information. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/cmdline", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "'%s' not found", buffer);
		values[i_fullcomm] = NULL;
	}
	else
	{
		values[i_fullcomm] =
				(char *) palloc((FULLCOMM_LEN + 1) * sizeof(char));
		len = read(fd, values[i_fullcomm], FULLCOMM_LEN);
		close(fd);
		values[i_fullcomm][len] = '\0';
	}
	elog(DEBUG5, "pg_proctab: %s %s", buffer, values[i_fullcomm]);

	/* Get the uid and username of the pid's owner. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d", PROCFS, pid);
	if (stat(buffer, &stat_struct) < 0)
	{
		elog(ERROR, "'%s' not found", buffer);
		strncpy(values[i_uid], "-1", INTEGER_LEN);
		values[i_username] = NULL;
	}
	else
	{
		struct passwd *pwd;

		snprintf(values[i_uid], INTEGER_LEN, "%d", stat_struct.st_uid);
		pwd = getpwuid(stat_struct.st_uid);
		if (pwd == NULL)
			values[i_username] = NULL;
		else
		{
			values[i_username] = (char *) palloc((strlen(pwd->pw_name) +
					1) * sizeof(char));
			strncpy(values[i_username], pwd->pw_name,
					sizeof(values[i_username]) - 1);
		}
	}

	/* Get the process table information for the pid. */
	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/stat", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		elog(ERROR, "%d/stat not found", pid);
		return 0;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	buffer[len] = '\0';
	elog(DEBUG5, "pg_proctab: %s", buffer);

	p = buffer;

	/* pid */
	GET_NEXT_VALUE(p, q, values[i_pid], length, "pid not found", ' ');

	/* comm */
	++p;
	if ((q = strchr(p, ')')) == NULL)
	{
		elog(ERROR, "pg_proctab: comm not found");
		return 0;
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
	GET_NEXT_VALUE(p, q, values[i_ppid], length, "ppid not found", ' ');

	/* pgrp */
	GET_NEXT_VALUE(p, q, values[i_pgrp], length, "pgrp not found", ' ');

	/* session */
	GET_NEXT_VALUE(p, q, values[i_session], length, "session not found", ' ');

	/* tty_nr */
	GET_NEXT_VALUE(p, q, values[i_tty_nr], length, "tty_nr not found", ' ');

	/* tpgid */
	GET_NEXT_VALUE(p, q, values[i_tpgid], length, "tpgid not found", ' ');

	/* flags */
	GET_NEXT_VALUE(p, q, values[i_flags], length, "flags not found", ' ');

	/* minflt */
	GET_NEXT_VALUE(p, q, values[i_minflt], length, "minflt not found", ' ');

	/* cminflt */
	GET_NEXT_VALUE(p, q, values[i_cminflt], length, "cminflt not found", ' ');

	/* majflt */
	GET_NEXT_VALUE(p, q, values[i_majflt], length, "majflt not found", ' ');

	/* cmajflt */
	GET_NEXT_VALUE(p, q, values[i_cmajflt], length, "cmajflt not found", ' ');

		/* utime */
	GET_NEXT_VALUE(p, q, values[i_utime], length, "utime not found", ' ');

	/* stime */
	GET_NEXT_VALUE(p, q, values[i_stime], length, "stime not found", ' ');

	/* cutime */
	GET_NEXT_VALUE(p, q, values[i_cutime], length, "cutime not found", ' ');

	/* cstime */
	GET_NEXT_VALUE(p, q, values[i_cstime], length, "cstime not found", ' ');

	/* priority */
	GET_NEXT_VALUE(p, q, values[i_priority], length, "priority not found", ' ');

	/* nice */
	GET_NEXT_VALUE(p, q, values[i_nice], length, "nice not found", ' ');

	/* num_threads */
	GET_NEXT_VALUE(p, q, values[i_num_threads], length,
				"num_threads not found", ' ');

	/* itrealvalue */
	GET_NEXT_VALUE(p, q, values[i_itrealvalue], length,
			"itrealvalue not found", ' ');

	/* starttime */
	GET_NEXT_VALUE(p, q, values[i_starttime], length, "starttime not found",
			' ');

	/* vsize */
	GET_NEXT_VALUE(p, q, values[i_vsize], length, "vsize not found", ' ');

	/* rss */
	GET_NEXT_VALUE(p, q, values[i_rss], length, "rss not found", ' ');

	SKIP_TOKEN(p);			/* skip rlim */
	SKIP_TOKEN(p);			/* skip startcode */
	SKIP_TOKEN(p);			/* skip endcode */
	SKIP_TOKEN(p);			/* skip startstack */
	SKIP_TOKEN(p);			/* skip kstkesp */
	SKIP_TOKEN(p);			/* skip kstkeip */
	SKIP_TOKEN(p);			/* skip signal (obsolete) */
	SKIP_TOKEN(p);			/* skip blocked (obsolete) */
	SKIP_TOKEN(p);			/* skip sigignore (obsolete) */
	SKIP_TOKEN(p);			/* skip sigcatch (obsolete) */
	SKIP_TOKEN(p);			/* skip wchan */
	SKIP_TOKEN(p);			/* skip nswap (place holder) */
	SKIP_TOKEN(p);			/* skip cnswap (place holder) */

	/* exit_signal */
	GET_NEXT_VALUE(p, q, values[i_exit_signal], length,
			"exit_signal not found", ' ');

	/* processor */
	GET_NEXT_VALUE(p, q, values[i_processor], length, "processor not found",
			' ');

	/* rt_priority */
	GET_NEXT_VALUE(p, q, values[i_rt_priority], length,
			"rt_priority not found", ' ');

	/* policy */
	GET_NEXT_VALUE(p, q, values[i_policy], length, "policy not found", ' ');

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

	/* Get i/o stats per process. */

	snprintf(buffer, sizeof(buffer) - 1, "%s/%d/io", PROCFS, pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		/* If the i/o stats are not available, set the values to zero. */
		elog(NOTICE, "i/o stats collection for Linux not enabled");
		strncpy(values[i_rchar], "0", BIGINT_LEN);
		strncpy(values[i_wchar], "0", BIGINT_LEN);
		strncpy(values[i_syscr], "0", BIGINT_LEN);
		strncpy(values[i_syscw], "0", BIGINT_LEN);
		strncpy(values[i_reads], "0", BIGINT_LEN);
		strncpy(values[i_writes], "0", BIGINT_LEN);
		strncpy(values[i_cwrites], "0", BIGINT_LEN);
	}
	else
	{
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
		buffer[len] = '\0';
		p = buffer;
		GET_VALUE(values[i_rchar]);
		GET_VALUE(values[i_wchar]);
		GET_VALUE(values[i_syscr]);
		GET_VALUE(values[i_syscw]);
		GET_VALUE(values[i_reads]);
		GET_VALUE(values[i_writes]);
		GET_VALUE(values[i_cwrites]);
	}

#endif /* __linux__ */

	elog(DEBUG5, "pg_proctab: [%d] uid %s", (int) i_uid, values[i_uid]);
	elog(DEBUG5, "pg_proctab: [%d] username %s", (int) i_username,
			values[i_username]);
	elog(DEBUG5, "pg_proctab: [%d] pid = %s", (int) i_pid, values[i_pid]);
	elog(DEBUG5, "pg_proctab: [%d] comm = %s", (int) i_comm, values[i_comm]);
	elog(DEBUG5, "pg_proctab: [%d] fullcomm = %s", (int) i_fullcomm,
			values[i_fullcomm]);
	elog(DEBUG5, "pg_proctab: [%d] state = %s", (int) i_state,
			values[i_state]);
	elog(DEBUG5, "pg_proctab: [%d] ppid = %s", (int) i_ppid, values[i_ppid]);
	elog(DEBUG5, "pg_proctab: [%d] pgrp = %s", (int) i_pgrp, values[i_pgrp]);
	elog(DEBUG5, "pg_proctab: [%d] session = %s", (int) i_session,
			values[i_session]);
	elog(DEBUG5, "pg_proctab: [%d] tty_nr = %s", (int) i_tty_nr,
			values[i_tty_nr]);
	elog(DEBUG5, "pg_proctab: [%d] tpgid = %s", (int) i_tpgid,
			values[i_tpgid]);
	elog(DEBUG5, "pg_proctab: [%d] flags = %s", (int) i_flags,
			values[i_flags]);
	elog(DEBUG5, "pg_proctab: [%d] minflt = %s", (int) i_minflt,
			values[i_minflt]);
	elog(DEBUG5, "pg_proctab: [%d] cminflt = %s", (int) i_cminflt,
			values[i_cminflt]);
	elog(DEBUG5, "pg_proctab: [%d] majflt = %s", (int) i_majflt,
			values[i_majflt]);
	elog(DEBUG5, "pg_proctab: [%d] cmajflt = %s", (int) i_cmajflt,
			values[i_cmajflt]);
	elog(DEBUG5, "pg_proctab: [%d] utime = %s", (int) i_utime,
			values[i_utime]);
	elog(DEBUG5, "pg_proctab: [%d] stime = %s", (int) i_stime,
			values[i_stime]);
	elog(DEBUG5, "pg_proctab: [%d] cutime = %s", (int) i_cutime,
			values[i_cutime]);
	elog(DEBUG5, "pg_proctab: [%d] cstime = %s", (int) i_cstime,
			values[i_cstime]);
	elog(DEBUG5, "pg_proctab: [%d] priority = %s", (int) i_priority,
			values[i_priority]);
	elog(DEBUG5, "pg_proctab: [%d] nice = %s", (int) i_nice, values[i_nice]);
	elog(DEBUG5, "pg_proctab: [%d] num_threads = %s", (int) i_num_threads,
			values[i_num_threads]);
	elog(DEBUG5, "pg_proctab: [%d] itrealvalue = %s", (int) i_itrealvalue,
			values[i_itrealvalue]);
	elog(DEBUG5, "pg_proctab: [%d] starttime = %s", (int) i_starttime,
			values[i_starttime]);
	elog(DEBUG5, "pg_proctab: [%d] vsize = %s", (int) i_vsize,
			values[i_vsize]);
	elog(DEBUG5, "pg_proctab: [%d] rss = %s", (int) i_rss, values[i_rss]);
	elog(DEBUG5, "pg_proctab: [%d] exit_signal = %s", (int) i_exit_signal,
			values[i_exit_signal]);
	elog(DEBUG5, "pg_proctab: [%d] processor = %s", (int) i_processor,
			values[i_processor]);
	elog(DEBUG5, "pg_proctab: [%d] rt_priority = %s", (int) i_rt_priority,
			values[i_rt_priority]);
	elog(DEBUG5, "pg_proctab: [%d] policy = %s", (int) i_policy,
			values[i_policy]);
	elog(DEBUG5, "pg_proctab: [%d] delayacct_blkio_ticks = %s",
			(int) i_delayacct_blkio_ticks, values[i_delayacct_blkio_ticks]);
	elog(DEBUG5, "pg_proctab: [%d] rchar = %s", (int) i_rchar,
			values[i_rchar]);
	elog(DEBUG5, "pg_proctab: [%d] wchar = %s", (int) i_wchar,
			values[i_wchar]);
	elog(DEBUG5, "pg_proctab: [%d] syscr = %s", (int) i_syscr,
			values[i_syscr]);
	elog(DEBUG5, "pg_proctab: [%d] syscw = %s", (int) i_syscw,
			values[i_syscw]);
	elog(DEBUG5, "pg_proctab: [%d] reads = %s", (int) i_reads,
			values[i_reads]);
	elog(DEBUG5, "pg_proctab: [%d] writes = %s", (int) i_writes,
			values[i_writes]);
	elog(DEBUG5, "pg_proctab: [%d] cwrites = %s", (int) i_cwrites,
			values[i_cwrites]);

	return 1;
}
