CC = gcc
CFLAGS = -Wall -g -O0
CFLAGS = -I.

#targets
all: myBridge

myBridge: main.o bridge.o
	$(CC) -o myBridge bridge.o main.o

bridge.o: bridge.c
	$(CC) $(CFLAGS) -c bridge.c

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -rf *.o myBridge
