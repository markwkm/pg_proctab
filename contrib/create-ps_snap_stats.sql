CREATE OR REPLACE FUNCTION ps_snap_stats(IN in_note TEXT) RETURNS BIGINT AS $$
DECLARE
  	snapid BIGINT;
BEGIN
	-- Create the snapshot id.
	INSERT INTO ps_snaps(note)
	VALUES(in_note)
	RETURNING snap
	INTO snapid;
	RAISE DEBUG 'Creating snapshot: %', snapid;

	-- Get system stats.
	INSERT INTO ps_cpustat(snap, cpu_user, cpu_nice, cpu_system, cpu_idle,
			cpu_iowait)
	SELECT snapid, "user", nice, system, idle, iowait
	FROM pg_cputime();

	INSERT INTO ps_memstat(snap, memused, memfree, memshared, membuffers,
			memcached, swapused, swapfree, swapcached)
	SELECT snapid, memused, memfree, memshared, membuffers, memcached,
			swapused, swapfree, swapcached
	FROM pg_memusage();

	-- Get database stats.
	INSERT INTO ps_dbstat(snap, datid, datname, numbackends, xact_commit,
			xact_rollback, blks_read, blks_hit)
	SELECT snapid, datid, datname, numbackends, xact_commit,
			xact_rollback, blks_read, blks_hit
	FROM pg_catalog.pg_stat_database;

	INSERT INTO ps_tablestat(snap, relid, schemaname, relname, seq_scan,
			seq_tup_read, idx_scan, idx_tup_fetch, n_tup_ins, n_tup_upd,
			n_tup_del, last_vacuum, last_autovacuum, last_analyze,
			last_autoanalyze)
	SELECT snapid, relid, schemaname, relname, seq_scan,
			seq_tup_read, idx_scan, idx_tup_fetch, n_tup_ins, n_tup_upd,
			n_tup_del, last_vacuum, last_autovacuum, last_analyze,
			last_autoanalyze
	FROM pg_catalog.pg_stat_all_tables;

	INSERT INTO ps_indexstat(snap, relid, indexrelid, schemaname, relname,
			indexrelname, idx_scan, idx_tup_read, idx_tup_fetch)
	SELECT snapid, relid, indexrelid, schemaname, relname,
			indexrelname, idx_scan, idx_tup_read, idx_tup_fetch
	FROM pg_catalog.pg_stat_all_indexes;

	-- Get process stats.
	INSERT INTO ps_procstat(snap, pid, comm, fullcomm, state, ppid, pgrp,
			session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt,
			utime, stime, cutime, cstime, priority, nice, num_threads,
			itrealvalue, starttime, vsize, rss, exit_signal, processor,
			rt_priority, policy, delayacct_blkio_ticks, uid, username,
			syscr, syscw, reads, writes, cwrites)
	SELECT snapid, procpid, comm, fullcomm, state, ppid, pgrp, session, tty_nr,
			tpgid, flags, minflt, cminflt, majflt, cmajflt, utime, stime,
			cutime, cstime, priority, nice, num_threads, itrealvalue,
			starttime, vsize, rss, exit_signal, processor, rt_priority,
			policy, delayacct_blkio_ticks, uid, username, syscr, syscw,
			reads, writes, cwrites
	FROM pg_stat_activity, pg_proctab()
	WHERE procpid = pid;

	-- Return the id of the snapshot just created.
	RETURN snapid;
END;
$$ LANGUAGE plpgsql;
