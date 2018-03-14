.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=sgensys
SHARE=sgensys
OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	loader/file.o \
	loader/symtab.o \
	loader/parser.o \
	loader/parseconv.o \
	loader/loader.o \
	mempool.o \
	ramp.o \
	wave.o \
	renderer/generator.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	sgensys.o
TEST1_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/lexer.o \
	mempool.o \
	test-scan.o

all: $(BIN)
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

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

loader/loader.o: loader/loader.c sgensys.h script.h ptrlist.h program.h ramp.h wave.h math.h loader/file.h common.h
	$(CC) -c $(CFLAGS) loader/loader.c -o loader/loader.o

loader/file.o: loader/file.c loader/file.h common.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/lexer.o: loader/lexer.c loader/lexer.h loader/file.h loader/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) loader/lexer.c -o loader/lexer.o

loader/parseconv.o: loader/parseconv.c program.h ramp.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) loader/parseconv.c -o loader/parseconv.o

loader/parser.o: loader/parser.c loader/file.h loader/symtab.h script.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) loader/parser.c -o loader/parser.o

loader/scanner.o: loader/scanner.c loader/scanner.h loader/file.h loader/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) loader/scanner.c -o loader/scanner.o

loader/symtab.o: loader/symtab.c loader/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) loader/symtab.c -o loader/symtab.o

mempool.o: mempool.c mempool.h arrtype.h common.h
	$(CC) -c $(CFLAGS) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrlist.h ramp.h sgensys.h wave.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: ramp.c ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/generator.c -o renderer/generator.o

sgensys.o: sgensys.c sgensys.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) sgensys.c

test-scan.o: test-scan.c sgensys.h loader/lexer.h loader/scanner.h loader/file.h loader/symtab.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c
