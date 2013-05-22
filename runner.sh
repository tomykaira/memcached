#!/bin/bash -x
#PBS -j oe
#PBS -l nodes=2:ppn=8


cd ${PBS_O_WORKDIR}

exec > output.$PBS_JOBID 2>&1

server=`hostname`
started=n

for host in `cat ${PBS_NODEFILE}`; do
        if [ $host = $me -a $started = n ]; then
                echo "It's me"
                ./memcached &
                started=y
        else
                rsh $host "cd ${PBS_O_WORKDIR}/client; ./mcb -c set -a $server -t 8 -n 10000 -l 500"
        fi
done
exit 0
