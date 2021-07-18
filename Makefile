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
	help.o \
	arrtype.o \
	ptrarr.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/parser.o \
	loader/parseconv.o \
	loader/loader.o \
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
	ptrarr.o \
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

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h ramp.h wave.h
	$(CC) -c $(CFLAGS) help.c

loader/loader.o: common.h loader/loader.c math.h program.h ptrarr.h ramp.h script.h sgensys.h wave.h
	$(CC) -c $(CFLAGS) loader/loader.c -o loader/loader.o

loader/file.o: common.h loader/file.c loader/file.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/lexer.o: common.h loader/file.h loader/lexer.c loader/lexer.h loader/scanner.h loader/symtab.h math.h mempool.h
	$(CC) -c $(CFLAGS) loader/lexer.c -o loader/lexer.o

loader/parseconv.o: arrtype.h common.h help.h loader/parseconv.c math.h program.h ptrarr.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/parseconv.c -o loader/parseconv.o

loader/parser.o: common.h help.h loader/file.h loader/parser.c loader/scanner.h loader/symtab.h math.h mempool.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/parser.c -o loader/parser.o

loader/scanner.o: common.h loader/file.h loader/scanner.c loader/scanner.h loader/symtab.h math.h mempool.h
	$(CC) -c $(CFLAGS) loader/scanner.c -o loader/scanner.o

loader/symtab.o: common.h mempool.h loader/symtab.c loader/symtab.h
	$(CC) -c $(CFLAGS) loader/symtab.c -o loader/symtab.o

mempool.o: mempool.c mempool.h common.h
	$(CC) -c $(CFLAGS) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrarr.h ramp.h sgensys.h wave.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

ramp.o: ramp.c ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/mixer.h renderer/osc.h program.h ramp.h wave.h math.h mempool.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/generator.c -o renderer/generator.o

renderer/mixer.o: renderer/mixer.c renderer/mixer.h ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/mixer.c -o renderer/mixer.o

renderer/osc.o: renderer/osc.c renderer/osc.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/osc.c -o renderer/osc.o

sgensys.o: common.h help.h math.h program.h ptrarr.h ramp.h sgensys.c sgensys.h wave.h
	$(CC) -c $(CFLAGS) sgensys.c

test-scan.o: common.h loader/file.h loader/lexer.h loader/scanner.h loader/symtab.h math.h program.h ptrarr.h ramp.h sgensys.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c
