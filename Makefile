CC = gcc
CFLAGS = -Wall -g -O0
CFLAGS = -I.

#targets
all: myBridge

myBridge: main.o bridge.o ll_map.o libgenl.o libnetlink.o
	$(CC) -o myBridge bridge.o main.o ll_map.o libgenl.o libnetlink.o

bridge.o: bridge.c
	$(CC) $(CFLAGS) -c bridge.c

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

ll_map.o: ll_map.c
	$(CC) $(CFLAGS) -c ll_map.c

libgenl.o: libgenl.c
	$(CC) $(CFLAGS) -c libgenl.c

libnetlink.o: libnetlink.c
	$(CC) $(CFLAGS) -c libnetlink.c

clean:
	rm -rf *.o myBridge
