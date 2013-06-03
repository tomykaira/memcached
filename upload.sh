#!/bin/sh

rsync -a . csc:memcached
ssh csc 'cd memcached && make && cd client && make'
