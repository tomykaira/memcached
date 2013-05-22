#!/bin/sh

rsync -a . csc:memcached
# ssh csc 'cd memcached; ./autogen.sh; ./configure --with-libevent=/home/tomykaira/libevent; make; qsub runner.sh'
ssh csc 'cd memcached && make && cd client && make && qsub runner.sh'
