#!/bin/bash -x

exec > result.$jobid 2>&1

times=10000

echo "500"
./mcb -c set -a $me -p $port -t 1 -n $times -l 500

echo "1000"
./mcb -c set -a $me -p $port -t 1 -n $times -l 1000

echo "4000"
./mcb -c set -a $me -p $port -t 1 -n $times -l 4000

echo "8000"
./mcb -c set -a $me -p $port -t 1 -n $times -l 8000

echo "16000"
./mcb -c set -a $me -p $port -t 1 -n $times -l 16000
