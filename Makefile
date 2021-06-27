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
	builder/file.o \
	builder/symtab.o \
	builder/parser.o \
	builder/parseconv.o \
	builder.o \
	mempool.o \
	ramp.o \
	wave.o \
	renderer/generator.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	sgensys.o
TEST_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/lexer.o \
	builder/parseconv.o \
	mempool.o \
	test-builder.o

all: $(BIN)
test: test-builder
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST_OBJ) test-builder
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

test-builder: $(TEST_OBJ)
	$(CC) $(TEST_OBJ) $(LFLAGS) -o test-builder

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c sgensys.h script.h ptrlist.h program.h ramp.h wave.h math.h builder/file.h common.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c program.h ramp.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c builder/file.h builder/symtab.h script.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/scanner.o: builder/scanner.c builder/scanner.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scanner.c -o builder/scanner.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h common.h
	$(CC) -c $(CFLAGS) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: ramp.c ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrlist.h ramp.h sgensys.h wave.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/generator.c -o renderer/generator.o

sgensys.o: sgensys.c sgensys.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) sgensys.c

test-builder.o: test-builder.c sgensys.h builder/lexer.h builder/scanner.h builder/file.h builder/symtab.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-builder.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c
