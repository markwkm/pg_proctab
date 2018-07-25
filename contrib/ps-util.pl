#!/usr/bin/env perl

use strict;
use warnings;

use Number::Bytes::Human qw(format_bytes);
use Getopt::Long;

use Data::Dumper;
$Data::Dumper::Indent	= 1;

my ($db, $list, $pid, $snap_1, $snap_2);

GetOptions(
	"db=s"	=> \$db,
	"list"	=> \$list,
	"pid=i"	=> \$pid,
	"s1=i"	=> \$snap_1,
	"s2=i"	=> \$snap_2,
);

#better way to do this?
if ((! $list) and ((! $pid) or (! $snap_1) or (! $snap_2))) {
	print <<USAGE
	You must supply a process id and two snapshot ids:
	Usage: $0 [-d=<database>] -list
	Usage: $0 [-d=<database>] -p=<pid> -s1=<snap1> -s2=<snap2>
USAGE
;
	exit 1;
}

$db	= "-d $db" if $db;

if ($list) {
	my $output = `psql $db -c "SELECT snap, time, note FROM ps_snaps ORDER BY time ASC"`;
	print ("$output\n");
	exit 0;
}

my $hz	= find_hz();
print ("HERTZ: $hz\n");

if ($snap_1 > $snap_2) {
	print ("snap1 was taken after snap2; swapping\n");
	($snap_1, $snap_2)	= ($snap_2, $snap_1);
}

my %data;
for ($snap_1, $snap_2) {
	my $snap	= $_;

	my $db_connect		= "psql $db --no-align --tuples-only --field-separator ' '";
	my $where_clause	= "WHERE pid = $pid AND a.snap = b.snap AND a.snap = $snap";

	my $raw_io_data	= `$db_connect --command "SELECT syscr, syscw, reads, writes, cwrites FROM ps_snaps a, ps_procstat b $where_clause"`;
	chomp($raw_io_data);
	@{ $data{$snap} }{'reads', 'writes', 'reads_B', 'writes_B', 'canned_B'} = split(/\s+/, $raw_io_data);

	my $raw_proc_data = `$db_connect --command "SELECT stime, utime, stime + utime AS total, extract(epoch FROM time) FROM ps_snaps a, ps_procstat b $where_clause"`;
	chomp($raw_proc_data);
	@{ $data{$snap} }{'ttime', 'time'} = split(/\s+/, $raw_proc_data);
}

for ($snap_1, $snap_2) {
	for my $key (keys %{ $data{$snap_1} }) {
		$data{'total'}{$key}	= ($data{$snap_2}{$key}	- $data{$snap_1}{$key});
	}
}
#print Dumper(%data);

$data{'total'}{'reads_B'}	= format_bytes($data{'total'}{'reads_B'});
$data{'total'}{'writes_B'}	= format_bytes($data{'total'}{'writes_B'});
$data{'total'}{'canned_B'}	= format_bytes($data{'total'}{'canned_B'});
my $timediff				= $data{'total'}{'time'} * $hz;
$data{'total'}{'util'}		= sprintf("%.2f", (($data{'total'}{'time'} / $timediff) * 100) );

print ("For PID $pid, snaps $snap_1 and $snap_2:\n");
print ("Total Reads: $data{'total'}{'reads'}\n");
print ("Total Writes: $data{'total'}{'writes'}\n");
print ("Reads in Bytes: $data{'total'}{'reads_B'}\n");
print ("Writes in Bytes: $data{'total'}{'writes_B'}\n");
print ("Cancelled in Bytes: $data{'total'}{'canned_B'}\n");
print ("Processor util: $data{'total'}{'util'}%\n");

sub find_hz {
	#hertz value used for timer will be in different places on different linux distros
	#CONFIG_HZ=[whatever] from the linux kernel .config
	#can be 100, 250, 300, or 1000

#config file is /usr/src/linux/.config on gentoo
#config file is /usr/src/linux-headers-`uname -r`/.config on ubuntu (and probably debian)

	my $kernel = `uname -r`;
	chomp $kernel;
#there has to be a more compact way to write this...
	my $file;
	if (-r '/usr/src/linux/.config') {
		$file	= '/usr/src/linux/.config';
	} elsif (-r "/usr/src/linux-headers-$kernel/.config") {
		$file	= "/usr/src/linux-headers-$kernel/.config";
	} else {
		print ("Unable to find linux kernel .config;  setting Hz to 1000\n");
		return '1000';
	}
	open (my $fh, '<', $file) or die ("Unable to open linux kernel config file $file. $!");
	my @stuff	= <$fh>;
	close $fh;
	my @hz	= grep(/^CONFIG_HZ=/, @stuff);
	chomp $hz[0];
	$hz[0]	=~ s/.*=//;

	return $hz[0];
}
