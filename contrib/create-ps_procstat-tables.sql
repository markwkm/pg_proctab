CREATE TABLE ps_snaps(
	snap BIGSERIAL PRIMARY KEY,
	note TEXT,
	time TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- PostgreSQL Processes Stats
CREATE TABLE ps_procstat(
	snap BIGINT,
	pid INTEGER,
	comm VARCHAR,
	fullcomm VARCHAR,
	state CHAR,
	ppid INTEGER,
	pgrp INTEGER,
	session INTEGER,
	tty_nr INTEGER,
	tpgid INTEGER,
	flags INTEGER,
	minflt BIGINT,
	cminflt BIGINT,
	majflt BIGINT,
	cmajflt BIGINT,
	utime BIGINT,
	stime BIGINT,
	cutime BIGINT,
	cstime BIGINT,
	priority BIGINT,
	nice BIGINT,
	num_threads BIGINT,
	itrealvalue BIGINT,
	starttime BIGINT,
	vsize BIGINT,
	rss BIGINT,
	exit_signal INTEGER,
	processor INTEGER,
	rt_priority BIGINT,
	policy BIGINT,
	delayacct_blkio_ticks BIGINT,
	uid INTEGER,
	username VARCHAR,
	rchar BIGINT,
	wchar BIGINT,
	syscr BIGINT,
	syscw BIGINT,
	reads BIGINT,
	writes BIGINT,
	cwrites BIGINT,
	datid BIGINT,
	datname NAME,
	usesysid BIGINT,
	usename NAME,
	current_query TEXT,
	waiting BOOLEAN,
	query_start TIMESTAMP WITH TIME ZONE,
	backend_start TIMESTAMP WITH TIME ZONE,
	client_addr INET,
	client_port INTEGER,
	CONSTRAINT proc_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);

-- PostgreSQL Database Stats
CREATE TABLE ps_dbstat(
	snap BIGINT,
	datid BIGINT,
	datname NAME,
	numbackends INTEGER,
	xact_commit BIGINT,
	xact_rollback BIGINT,
	blks_read BIGINT,
	blks_hit BIGINT,
	PRIMARY KEY (snap, datid),
	CONSTRAINT db_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);

-- PostgreSQL Table Stats
CREATE TABLE ps_tablestat(
	snap BIGINT,
	relid BIGINT,
	schemaname NAME,
	relname NAME,
	seq_scan BIGINT,
	seq_tup_read BIGINT,
	idx_scan BIGINT,
	idx_tup_fetch BIGINT,
	n_tup_ins BIGINT,
	n_tup_upd BIGINT,
	n_tup_del BIGINT,
	last_vacuum TIMESTAMP WITH TIME ZONE,
	last_autovacuum TIMESTAMP WITH TIME ZONE,
	last_analyze TIMESTAMP WITH TIME ZONE,
	last_autoanalyze TIMESTAMP WITH TIME ZONE,
	PRIMARY KEY (snap, relid),
	CONSTRAINT table_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);

-- PostgreSQL Index Stats
CREATE TABLE ps_indexstat(
	snap BIGINT,
	relid BIGINT,
	indexrelid BIGINT,
	schemaname NAME,
	relname NAME,
	indexrelname NAME,
	idx_scan BIGINT,
	idx_tup_read BIGINT,
	idx_tup_fetch BIGINT,
	PRIMARY KEY (snap, relid, indexrelid),
	CONSTRAINT index_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);

-- System Processor Stats
CREATE TABLE ps_cpustat(
	snap BIGINT PRIMARY KEY,
	cpu_user BIGINT,
	cpu_nice BIGINT,
	cpu_system BIGINT,
	cpu_idle BIGINT,
	cpu_iowait BIGINT,
	cpu_swap BIGINT,
	CONSTRAINT cpu_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);

-- System Memory Stats
CREATE TABLE ps_memstat(
	snap BIGINT PRIMARY KEY,
	memused BIGINT,
	memfree BIGINT,
	memshared BIGINT,
	membuffers BIGINT,
	memcached BIGINT,
	swapused BIGINT,
	swapfree BIGINT,
	swapcached BIGINT,
	CONSTRAINT mem_snap
		FOREIGN KEY (snap)
		REFERENCES ps_snaps (snap)
);
