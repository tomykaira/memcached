#!/bin/bash
#PBS -j oe
#PBS -l nodes=1:ppn=1


cd ${PBS_O_WORKDIR}

exec > output.$PBS_JOBID 2>&1

cat ${PBS_NODEFILE}
./memcached -vv
exit 0
