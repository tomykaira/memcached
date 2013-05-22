.PHONY: all clean
CFLAGS=-g -Wall -std=gnu99

all: client

client: client.o comm.o
	gcc $(CFLGAS) -o $@ $^
