#!/bin/sh

rsync -a . csc:memcached
ssh csc 'cd memcached && cd client && make && cd .. && qsub runner.sh'
