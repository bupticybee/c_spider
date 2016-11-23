CC = g++
CFLAGS = -O3 -march=core2 -Wall -g -std=c++11 
spider: spider.o
	$(CC) -o $@ $< -levent -lpthread $(CFLAGS)
spider.o: spider.c bloom.h dns.h url.h http.h  threadpool.c
threadpool.o: 
	$(CC) -o threadpool.o  threadpool.c   -lpthread $(CFLAGS)
clean: 
	-rm spider.o spider downpage/*
