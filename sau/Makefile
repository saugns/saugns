.POSIX:
AR=ar
CC=cc
CFLAGS_COMMON=-std=c99 -W -Wall -I..
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3
CFLAGS_FASTF=$(CFLAGS_COMMON) -O3 -ffast-math
CFLAGS_SIZE=$(CFLAGS_COMMON) -Os
PREFIX=/usr/local
LIB=libsau.a
LIB_TESTS=libsau-tests.a
OBJ=\
	arrtype.o \
	error.o \
	math.o \
	help.o \
	file.o \
	symtab.o \
	scanner.o \
	parser.o \
	parseconv.o \
	mempool.o \
	ramp.o \
	wave.o \
	generator.o
OBJ_TESTS=\
	lexer.o

all: $(LIB) $(LIB_TESTS)
clean:
	rm -f $(OBJ) $(LIB)
	rm -f $(OBJ_TESTS) $(LIB_TESTS)
install: $(LIB)
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include/sau
	cp -f $(LIB) $(DESTDIR)$(PREFIX)/lib
	cp -f *.h $(DESTDIR)$(PREFIX)/include/sau
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIB)
	rm -Rf $(DESTDIR)$(PREFIX)/include/sau

$(LIB): $(OBJ)
	$(AR) crs $(LIB) $(OBJ)

$(LIB_TESTS): $(OBJ) $(OBJ_TESTS)
	$(AR) crs $(LIB_TESTS) $(OBJ) $(OBJ_TESTS)

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

error.o: common.h error.c
	$(CC) -c $(CFLAGS_SIZE) error.c

help.o: common.h help.c help.h math.h ramp.h wave.h
	$(CC) -c $(CFLAGS_SIZE) help.c

math.o: common.h math.c math.h
	$(CC) -c $(CFLAGS_FAST) math.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

file.o: common.h file.c file.h
	$(CC) -c $(CFLAGS) file.c

lexer.o: common.h math.h mempool.h file.h lexer.c lexer.h scanner.h symtab.h
	$(CC) -c $(CFLAGS) lexer.c

parseconv.o: arrtype.h common.h math.h mempool.h parseconv.c program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) parseconv.c

parser.o: common.h math.h mempool.h program.h ramp.h file.h parser.c scanner.h symtab.h script.h wave.h
	$(CC) -c $(CFLAGS_SIZE) parser.c

scanner.o: common.h math.h mempool.h file.h scanner.h scanner.c symtab.h
	$(CC) -c $(CFLAGS_FAST) scanner.c

symtab.o: common.h mempool.h symtab.h symtab.c
	$(CC) -c $(CFLAGS_FAST) symtab.c

generator.o: common.h generator.c generator.h generator/osc.h math.h mempool.h program.h ramp.h wave.h
	$(CC) -c $(CFLAGS_FASTF) generator.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
