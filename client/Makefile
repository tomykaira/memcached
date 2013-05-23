.PHONY: all clean
CFLAGS=-g -std=gnu99 -Wall -Werror -pedantic -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls
LDFLAGS=-libverbs

all: client
clean:
	rm *.o client mcb

client: client.o comm.o ../ib.o

mcb: mcb.o
	gcc $(CFLGAS) -o $@ $^ -lpthread
