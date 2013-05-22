.PHONY: all clean
CFLAGS=-g -Wall -std=gnu99

all: client mcb

client: client.o comm.o
	gcc $(CFLGAS) -o $@ $^

mcb: mcb.o
	gcc $(CFLGAS) -o $@ $^ -lpthread
