#!/bin/bash -x
#PBS -j oe
#PBS -l nodes=2:ppn=1


cd ${PBS_O_WORKDIR}

exec > output.$PBS_JOBID 2>&1

me=`hostname`
started=n
port=`echo $RANDOM % 1000  + 9292 | bc`

export $PBS_JOBID
export $me
export $port

for host in `cat ${PBS_NODEFILE}`; do
  if [ $host = $me ]; then
    if [ $started = n ]; then
      echo "It's me"
      ./memcached -p $port -U 0 &
      started=y
    fi
  else
    rsh $host "export me=$me port=$port jobid=$PBS_JOBID; sleep 1; cd ${PBS_O_WORKDIR}/client; ./runner.sh" &
  fi
done

sleep 4

while [ -n "$(ps | grep rsh)" ]; do
  sleep 10
  echo "Waiting"
done
exit 0
