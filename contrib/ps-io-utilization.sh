#!/bin/bash

if [ $# -ne 3 ]; then
	echo "Usage: $0 <pid> <snapid1> <snapid2>"
	exit 1
fi

PID=$1
SNAP1=$2
SNAP2=$3

A=( `psql --no-align --tuples-only --field-separator ' ' --command "SELECT syscr, syscw, reads, writes, cwrites FROM ps_snaps a, ps_procstat b WHERE pid = ${PID} AND a.snap = b.snap AND a.snap = ${SNAP1}"` )

R1=${A[0]}
W1=${A[1]}
RB1=${A[2]}
WB1=${A[3]}
CWB1=${A[4]}

A=( `psql --no-align --tuples-only --field-separator ' ' --command "SELECT syscr, syscw, reads, writes, cwrites FROM ps_snaps a, ps_procstat b WHERE pid = ${PID} AND a.snap = b.snap AND a.snap = ${SNAP2}"` )

R2=${A[0]}
W2=${A[1]}
RB2=${A[2]}
WB2=${A[3]}
CWB2=${A[4]}

R=$(( ${R2} - ${R1} ))
W=$(( ${W2} - ${W1} ))
RB=$(( ${RB2} - ${RB1} ))
WB=$(( ${WB2} - ${WB1} ))
CWB=$(( ${CWB2} - ${CWB1} ))

echo "Reads = ${R}"
echo "Writes = ${W}"
echo "Reads (Bytes) = ${RB}"
echo "Writes (Bytes) = ${WB}"
echo "Cancelled (Bytes) = ${CWB}"
