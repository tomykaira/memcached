#!/bin/bash -x

exec > result.$jobid 2>&1

./client $me $port
