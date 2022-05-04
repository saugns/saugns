.POSIX:
CC=cc
CFLAGS_COMMON=-std=c99 -W -Wall
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3
CFLAGS_FASTF=$(CFLAGS_COMMON) -O3 -ffast-math
CFLAGS_SIZE=$(CFLAGS_COMMON) -Os
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=sgensys
SHARE=sgensys
OBJ=\
	arrtype.o \
	error.o \
	help.o \
	file.o \
	symtab.o \
	parser.o \
	mempool.o \
	ramp.o \
	wave.o \
	generator.o \
	player/audiodev.o \
	player/sndfile.o \
	sgensys.o
TEST1_OBJ=\
	arrtype.o \
	error.o \
	file.o \
	symtab.o \
	scanner.o \
	lexer.o \
	mempool.o \
	test-scan.o

all: $(BIN)
check: $(BIN)
	./$(BIN) -c $(ARGS) */*.sgs examples/*/*.sgs
fullcheck: $(BIN)
	./$(BIN) -m $(ARGS) */*.sgs examples/*/*.sgs
tests: test-scan
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST1_OBJ) test-scan
install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/$(SHARE)
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	cp -RP examples/* $(DESTDIR)$(PREFIX)/share/$(SHARE)
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -Rf $(DESTDIR)$(PREFIX)/share/$(SHARE)

$(BIN): $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o $(BIN); \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o $(BIN); \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o $(BIN); \
	else \
		echo "Linking for UNIX with OSS."; \
		$(CC) $(OBJ) $(LFLAGS) -o $(BIN); \
	fi

test-scan: $(TEST1_OBJ)
	$(CC) $(TEST1_OBJ) $(LFLAGS) -o test-scan

arrtype.o: arrtype.c arrtype.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) arrtype.c

error.o: sgensys.h error.c
	$(CC) -c $(CFLAGS_SIZE) error.c

help.o: sgensys.h help.c help.h ramp.h wave.h
	$(CC) -c $(CFLAGS_SIZE) help.c

mempool.o: sgensys.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

player/audiodev.o: sgensys.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/sndfile.o: sgensys.h player/sndfile.c player/sndfile.h
	$(CC) -c $(CFLAGS) player/sndfile.c -o player/sndfile.o

ramp.o: sgensys.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

file.o: sgensys.h file.c file.h
	$(CC) -c $(CFLAGS) file.c

lexer.o: sgensys.h math.h mempool.h file.h lexer.c lexer.h symtab.h
	$(CC) -c $(CFLAGS) lexer.c

parser.o: sgensys.h arrtype.h math.h mempool.h program.h ramp.h file.h parser.c parser/parseconv.h symtab.h script.h wave.h
	$(CC) -c $(CFLAGS_SIZE) parser.c

scanner.o: sgensys.h math.h mempool.h file.h scanner.c scanner.h symtab.h
	$(CC) -c $(CFLAGS_FAST) scanner.c

symtab.o: sgensys.h mempool.h symtab.c symtab.h
	$(CC) -c $(CFLAGS_FAST) symtab.c

generator.o: sgensys.h math.h mempool.h program.h ramp.h generator.c generator.h generator/osc.c generator/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) generator.c

sgensys.o: sgensys.c help.h generator.h script.h arrtype.h program.h ramp.h wave.h math.h file.h player/audiodev.h player/sndfile.h sgensys.h
	$(CC) -c $(CFLAGS_SIZE) sgensys.c

test-scan.o: sgensys.h math.h program.h ramp.h file.h lexer.h scanner.h symtab.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: sgensys.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
