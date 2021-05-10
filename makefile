#Makefile Iozia Simone N.549108

SHELL   := /bin/bash
CC	 = gcc
CFLAGS   = -g -lpthread -pthread -pedantic -Wall -O3 -std=c99 -D_POSIX_C_SOURCE=200809L
OBJFILES = direttore.o supermercato.o
VALGRIND = valgrind --leak-check=full 

.PHONY: test1 test2 clean

all: $(OBJFILES)

direttore.o: direttore.c 
	$(CC) $(CFLAGS) $< -o $@
	
supermercato.o: supermercato.c 
	$(CC) $(CFLAGS) $< -o $@  
	
test1: 
	(./direttore.o ./config1.txt & echo $$! > direttore.PID) &
	($(VALGRIND) ./supermercato.o ./config1.txt) &
	sleep 15s; \
	kill -3 $$(cat direttore.PID); \
	
test2:
	(./direttore.o ./config2.txt & echo $$! > direttore.PID) &
	sleep 25s; \
	kill -1 $$(cat direttore.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat direttore.PID); \
	
clean:
	rm -f *.o *.log mysock direttore.PID
