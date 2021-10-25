#makefile for project1

CC=gcc
CGLAGS = -g -Wall
TEST_ADDR = 127.0.0.1
TEST_PORT = 3761

project: client

build: client.c
	$(CC) $(CFLAGS) -o client client.c
.PHONY:build

client: build
	./client $(TEST_ADDR) $(TEST_PORT)
.PHONY:client

server:
	./server $(TEST_PORT)
.PHONY:server

debug:
	./server $(TEST_PORT) verbose
.PHONY:debug

clean:
	rm -f client
.PHONY:clean
