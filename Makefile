.POSIX:
CC=cc
CFLAGS_COMMON=-std=c99 -W -Wall
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3
CFLAGS_FASTF=$(CFLAGS_COMMON) -ffast-math -O3
CFLAGS_SIZE=$(CFLAGS_COMMON) -Os
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
	builder/scanner.o \
	builder/parser.o \
	builder/voicegraph.o \
	builder/parseconv.o \
	builder/builder.o \
	mempool.o \
	ramp.o \
	wave.o \
	renderer/osc.o \
	renderer/mixer.o \
	renderer/generator.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	sgensys.o
TEST1_OBJ=\
	common.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/lexer.o \
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

builder/builder.o: builder/builder.c common.h math.h program.h ptrlist.h ramp.h script.h sgensys.h wave.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/file.h builder/lexer.c builder/lexer.h builder/symtab.h builder/scanner.h common.h math.h mempool.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: arrtype.h builder/parseconv.c builder/parseconv.h common.h math.h program.h ptrlist.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/file.h builder/parser.c builder/scanner.h builder/symtab.h common.h math.h mempool.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS_SIZE) builder/parser.c -o builder/parser.o

builder/scanner.o: builder/file.h builder/scanner.c builder/scanner.h builder/symtab.h common.h math.h mempool.h
	$(CC) -c $(CFLAGS_FAST) builder/scanner.c -o builder/scanner.o

builder/symtab.o: builder/symtab.c builder/symtab.h common.h mempool.h
	$(CC) -c $(CFLAGS_FAST) builder/symtab.c -o builder/symtab.o

builder/voicegraph.o: arrtype.h builder/voicegraph.c builder/parseconv.h common.h math.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) builder/voicegraph.c -o builder/voicegraph.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrlist.h ramp.h sgensys.h wave.h
	$(CC) -c $(CFLAGS) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrlist.o: common.h ptrlist.c ptrlist.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

renderer/generator.o: common.h math.h ramp.h renderer/generator.c renderer/generator.h renderer/mixer.h renderer/osc.h program.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/generator.c -o renderer/generator.o

renderer/mixer.o: common.h math.h ramp.h renderer/mixer.c renderer/mixer.h
	$(CC) -c $(CFLAGS_FASTF) renderer/mixer.c -o renderer/mixer.o

renderer/osc.o: common.h math.h renderer/osc.c renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/osc.c -o renderer/osc.o

sgensys.o: sgensys.c sgensys.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) sgensys.c

test-scan.o: common.h builder/file.h builder/lexer.h builder/scanner.h builder/symtab.h math.h program.h ptrlist.h ramp.h sgensys.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
