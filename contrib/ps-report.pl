#!/usr/bin/env perl

use strict;
use warnings;

unless ($ENV{PGDATABASE}) {
	print "PGDATABASE must be set in your environment to determine which \n";
	print "database to report statistics for.\n";
	exit(1);
}

if (scalar @ARGV != 3) {
	print "Usage: $0 <pid> <snap1> <snap2>\n";
	exit(1);
}

my $pid = $ARGV[0];
my $snap1 = $ARGV[1];
my $snap2 = $ARGV[2];

my $psql = "psql --tuples-only --no-align --command";
my $sql;
my $temp;
my @a1;
my @a2;

# Get the snapshot timestamps.
$temp = "
SELECT time
FROM ps_snaps
WHERE snap = %d
";

$sql = sprintf($temp, $snap1);
my $start_time = `$psql "$sql"`;
chomp $start_time;

$sql = sprintf($temp, $snap2);
my $end_time = `$psql "$sql"`;
chomp $end_time;

# Get database stats.

$temp = "
SELECT numbackends, xact_commit, xact_rollback, blks_read, blks_hit
FROM ps_dbstat
WHERE datname = '$ENV{PGDATABASE}'
  AND snap = %d
";

$sql = sprintf($temp, $snap1);
@a1 = split /\|/, `$psql "$sql"`;

$sql = sprintf($temp, $snap2);
@a2 = split /\|/, `$psql "$sql"`;

my $xact_commit = $a2[1] - $a1[1];
my $xact_rollback = $a2[2] - $a1[2];
my $blks_read = $a2[3] - $a1[3];
my $blks_hit = $a2[4] - $a1[4];

print "Database       : $ENV{PGDATABASE}\n";
print "Snapshot Start : $start_time\n";
print "Snapshot End   : $end_time\n";
print "\n";
print "-------------------\n";
print "Database Statistics\n";
print "-------------------\n";
print "Commits     : $xact_commit\n";
print "Rollbacks   : $xact_rollback\n";
print "Blocks Read : $blks_read\n";
print "Blocks Hit  : $blks_hit\n";
print "\n";

# FIXME: This simple logic breakds if tables are added or dropped between
# snapshots.

$temp = "
SELECT schemaname, relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch,
	n_tup_ins, n_tup_upd, n_tup_del
FROM ps_tablestat
WHERE snap = %d
ORDER BY schemaname, relname
";

$sql = sprintf($temp, $snap1);
@a1 = split /\n/, `$psql "$sql"`;

# Get the vaccum dates of the second snapshot.
$temp = "
SELECT schemaname, relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch,
	n_tup_ins, n_tup_upd, n_tup_del, last_vacuum, last_autovacuum,
	last_analyze, last_autoanalyze
FROM ps_tablestat
WHERE snap = %d
ORDER BY schemaname, relname
";
$sql = sprintf($temp, $snap2);
@a2 = split /\n/, `$psql "$sql"`;

unless (scalar @a1 == scalar @a2) {
	print "Tables were added or dropped between snapshot $snap1 and $snap2\n";
	print "This script can't handle that, aborting.\n";
	exit(1);
}

my $length = 0;

print "================\n";
print "Table Statistics\n";
print "================\n";

# Run through all the names once just to see what the longest one is to
# determine how to format the output.
my @c = ();
for (my $i = 0; $i < scalar @a1; $i++) {
	my @b1 = split /\|/, $a1[$i];
	my @b2 = split /\|/, $a2[$i];

	# Some columns are NULL, set to 0.
	for (my $i = 0; $i < 9; $i++) {
		$b1[$i] = 0 unless ($b1[$i]);
		$b2[$i] = 0 unless ($b2[$i]);
	}
	for (my $i = 9; $i < 13; $i++) {
		$b2[$i] = 'N/A' unless ($b2[$i]);
	}
	my $name = "$b1[0].$b1[1]";
	$length = length $name if (length $name > $length);
	push @c, ([$name, $b2[2] - $b1[2], $b2[3] - $b1[3], $b2[4] - $b1[4],
			$b2[5] - $b1[5], $b2[6] - $b1[6], $b2[7] - $b1[7],
			$b2[8] - $b1[8], "$b2[9]", "$b2[10]", "$b2[11]", "$b2[12]"]);
}

my @header = ("Schema.Relation", "Seq Scan", "Seq Tup Read",
		"Idx Scan", "Idx Tup Fetch", "N Tup Ins", "N Tup Upd", "N Tup Del",
		"Last Vacuum", "Last Autovacuum", "Last Analyze", "Last Autoanalyze");
my @l = ();
my @d = ();
foreach (@header) {
	push @l, length $_;
	my $dashes = '';
	for (my $i = 0; $i < length $_; $i++) {
		$dashes .= '-';
	}
	push @d, $dashes;
}
# Re-adjust the lengths for timestamps.
for (my $i = 8; $i < 12; $i++) {
	$l[$i] = 29;
}
# Re-adjust the dashes for the Schema.Relation to the longest one.
$d[0] = '';
for (my $i = 0; $i < $length; $i++) {
	$d[0] .= '-';
}
my $header_format = "%-" . $length . "s %" . $l[1] . "s %" . $l[2] . "s %" .
		$l[3] . "s %" . $l[4] . "s %" . $l[5] . "s %" . $l[6] . "s %" .
		$l[7] . "s %" . $l[8] . "s %" . $l[9] . "s %" . $l[10] . "s %" .
		$l[11] . "s\n";
my $format = "%-" . $length . "s %" . $l[1] . "d %" . $l[2] . "d %" .
		$l[3] . "d %" . $l[4] . "d %" . $l[5] . "d %" . $l[6] . "d %" .
		$l[7] . "d %" . $l[8] . "s %" . $l[9] . "s %" . $l[10] . "s %" .
		$l[11] . "s\n";

# Now start displaying data.
printf $header_format, @d;
printf $header_format, @header;
printf $header_format, @d;
for (my $i = 0; $i < scalar @c; $i++) {
	printf $format, @{$c[$i]};
}
print"\n";

# Display index stats.

# FIXME: This simple logic breakds if indexes are added or dropped between
# snapshots.

$temp = "
SELECT schemaname, relname, indexrelname, idx_scan, idx_tup_read, idx_tup_fetch
FROM ps_indexstat
WHERE snap = %d
ORDER BY schemaname, relname, indexrelname
";

$sql = sprintf($temp, $snap1);
@a1 = split /\n/, `$psql "$sql"`;

$sql = sprintf($temp, $snap2);
@a2 = split /\n/, `$psql "$sql"`;

print "================\n";
print "Index Statistics\n";
print "================\n";

# Run through all the names once just to see what the longest one is to
# determine how to format the output.
@c = ();
for (my $i = 0; $i < scalar @a1; $i++) {
	my @b1 = split /\|/, $a1[$i];
	my @b2 = split /\|/, $a2[$i];

	my $name = "$b1[0].$b1[1].$b1[2]";
	$length = length $name if (length $name > $length);
	push @c, ([$name, $b2[3] - $b1[3], $b2[4] - $b1[4], $b2[5] - $b1[5]]);
}
@header = ("Schema.Relation.Index", "Idx Scan", "Idx Tup Read",
		"Idx Tup Fetch");
@l = ();
@d = ();
foreach (@header) {
	push @l, length $_;
	my $dashes = '';
	for (my $i = 0; $i < length $_; $i++) {
		$dashes .= '-';
	}
	push @d, $dashes;
}
# Re-adjust the dashes for the Schema.Relation.Index to the longest one.
$d[0] = '';
for (my $i = 0; $i < $length; $i++) {
	$d[0] .= '-';
}
$header_format = "%-" . $length . "s %" . $l[1] . "s %" .  $l[2] . "s %" .
		$l[3] . "s\n";
$format = "%-" . $length . "s %" . $l[1] . "d %" .  $l[2] . "d %" . $l[3] .
		"d\n";

# Now start displaying data.
printf $header_format, @d;
printf $header_format, @header;
printf $header_format, @d;
for (my $i = 0; $i < scalar @c; $i++) {
	printf $format, @{$c[$i]};
}
