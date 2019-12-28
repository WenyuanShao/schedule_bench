OBJS=heap.o test_bitmap.o
CC=gcc
CFLAGS=-O3 -Wall

test:$(OBJS)
	$(CC) $(OBJS) -o test

heap.o:heap.c heap.h
	$(CC) $(CFLAGS) -c heap.c -o heap.o

test_bitmap:test_bitmap.c heap.h
	$(CC) $(CFLAGS) -c test_bitmap.c -o test_bitmap.o

clean:
	rm -rf *.o test
