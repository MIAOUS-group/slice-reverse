CC = gcc
CFLAGS= -Wall -Wextra -O0 -g -lm -fPIC
LIST = reverse scan

all: ${LIST}

util.o: util.c util.h
monitoring.o: util.o monitoring.c monitoring.h global_variables.h
poke.o: util.o poke.c poke.h
wrmsr.o:wrmsr.c wrmsr.h
rdmsr.o:rdmsr.c rdmsr.h
scan.o: scan.c scan.h global_variables.h
reverse.o:reverse.c reverse.h global_variables.h
arch.o: arch.c arch.h

reverse: reverse.o util.o poke.o wrmsr.o rdmsr.o monitoring.o arch.o
	${CC} -Wall -O0 -g reverse.o util.o poke.o wrmsr.o rdmsr.o arch.o monitoring.o -o reverse -lm

scan: monitoring.o scan.o util.o poke.o wrmsr.o rdmsr.o arch.o
	${CC} -Wall -O0 -g scan.o util.o poke.o wrmsr.o rdmsr.o arch.o monitoring.o -o scan -lm



clean:
	rm -f ${LIST} *.o
