SYS_LINK=-L/usr/lib
INCS = -I../include
OBJS = ../obj 
FLAGS = -g 
CC = gcc

all : tsh tshlib.o tsh_test copy bin/matrix_master matrix_master matrix_worker

# Main TSH server
tsh : tsh.c tsh.h
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o tsh tsh.c -L$(OBJS) -lsng -lm

# TSH library - just connection functionality for now
tshlib.o : tshlib.c tshlib.h
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -c tshlib.c

# Test program for tshlib
tsh_test : tsh_test.c tshlib.o
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o tsh_test tsh_test.c tshlib.o -L$(OBJS) -lsng -lm

# Original TSH test program
tshtest : tshtest.c tshtest.h
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o tshtest tshtest.c -L$(OBJS) -lsng -lm

# Matrix master binary
bin/matrix_master: matrix_master.c tshlib.o ../obj/libsng.a
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o ../bin/matrix_master matrix_master.c tshlib.o -L../obj -lsng -lm

# Matrix master in current directory
matrix_master: matrix_master.c tshlib.o
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o matrix_master matrix_master.c tshlib.o -L$(OBJS) -lsng -lm

# Matrix worker binary
matrix_worker: matrix_worker.c tshlib.o
	$(CC) $(EXTRA) $(INCS) $(FLAGS) -o matrix_worker matrix_worker.c tshlib.o -L$(OBJS) -lsng -lm

# Copy executables to bin directory
copy :
	mkdir -p ../bin
	cp -f tsh ../bin/tsh
	cp -f tsh_test ../bin/tsh_test
	@if [ -f tshtest ]; then cp -f tshtest ../bin/tshtest; fi

clean :
	rm -f *.o tsh tsh_test tshtest bin/matrix_master matrix_master matrix_worker
	rm -f matrix_performance.csv matrix_performance_fault_tolerance.csv

.PHONY: all clean copy