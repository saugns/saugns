.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=saugns
SHARE=saugns
OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/parser.o \
	builder/parseconv.o \
	builder/scriptconv.o \
	builder.o \
	mempool.o \
	ramp.o \
	wave.o \
	renderer.o \
	renderer/osc.o \
	renderer/mixer.o \
	renderer/generator.o \
	audiodev.o \
	wavfile.o \
	saugns.o
TEST_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/lexer.o \
	builder/scriptconv.o \
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

audiodev.o: audiodev.c audiodev/*.c audiodev.h common.h
	$(CC) -c $(CFLAGS) audiodev.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c saugns.h script.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/file.h builder/symtab.h builder/scanner.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c builder/parser.h program.h ramp.h wave.h math.h script.h ptrlist.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c builder/parser.h builder/scanner.h builder/file.h builder/symtab.h script.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/scanner.o: builder/scanner.c builder/scanner.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scanner.c -o builder/scanner.o

builder/scriptconv.o: builder/scriptconv.c program.h ramp.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/scriptconv.c -o builder/scriptconv.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h arrtype.h common.h
	$(CC) -c $(CFLAGS) mempool.c

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: ramp.c ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

renderer.o: renderer.c saugns.h renderer/generator.h ptrlist.h program.h ramp.h wave.h math.h audiodev.h wavfile.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/mixer.h renderer/osc.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/generator.c -o renderer/generator.o

renderer/mixer.o: renderer/mixer.c renderer/mixer.h ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/mixer.c -o renderer/mixer.o

renderer/osc.o: renderer/osc.c renderer/osc.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/osc.c -o renderer/osc.o

saugns.o: saugns.c saugns.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) saugns.c

test-builder.o: test-builder.c saugns.h builder/lexer.h builder/scanner.h builder/file.h builder/symtab.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-builder.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c

wavfile.o: wavfile.c wavfile.h common.h
	$(CC) -c $(CFLAGS) wavfile.c
