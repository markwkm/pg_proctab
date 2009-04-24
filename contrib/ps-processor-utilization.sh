#!/bin/bash

# FIXME: Hardcoding, best way to get this?
HZ=1000

if [ $# -ne 3 ]; then
	echo "Usage: $0 <pid> <snapid1> <snapid2>"
	exit 1
fi

PID=$1
SNAP1=$2
SNAP2=$3

A=( `psql --no-align --tuples-only --field-separator ' ' --command "SELECT stime, utime, stime + utime AS total, extract(epoch FROM time) FROM ps_snaps a, ps_procstat b WHERE pid = ${PID} AND a.snap = b.snap AND a.snap = ${SNAP1}"` )

STIME1=${A[0]}
UTIME1=${A[1]}
TOTAL1=${A[2]}
TIME1=${A[3]}

A=( `psql --no-align --tuples-only --field-separator ' ' --command "SELECT stime, utime, stime + utime AS total, extract(epoch FROM time) FROM ps_snaps a, ps_procstat b WHERE pid = ${PID} AND a.snap = b.snap AND a.snap = ${SNAP2}"` )

STIME2=${A[0]}
UTIME2=${A[1]}
TOTAL2=${A[2]}
TIME2=${A[3]}

# Get the time difference in ticks.
TIMEDIFF=`echo "scale = 2; (${TIME2} - ${TIME1}) * ${HZ}" | bc -l`

U=`echo "scale = 2; (${TOTAL2} - ${TOTAL1}) / ${TIMEDIFF} * 100" | bc -l`

echo "Processor Utilization = ${U} %"
