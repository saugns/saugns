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
	common.o \
	help.o \
	arrtype.o \
	ptrarr.o \
	reader/file.o \
	reader/symtab.o \
	reader/parser.o \
	reader/parseconv.o \
	reader/reader.o \
	mempool.o \
	ramp.o \
	wave.o \
	renderer/osc.o \
	renderer/generator.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	sgensys.o
TEST1_OBJ=\
	common.o \
	ptrarr.o \
	reader/file.o \
	reader/symtab.o \
	reader/scanner.o \
	reader/lexer.o \
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

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h ramp.h wave.h
	$(CC) -c $(CFLAGS) help.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrarr.h ramp.h sgensys.h wave.h
	$(CC) -c $(CFLAGS) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

reader/file.o: common.h reader/file.c reader/file.h
	$(CC) -c $(CFLAGS) reader/file.c -o reader/file.o

reader/lexer.o: common.h math.h mempool.h reader/file.h reader/lexer.c reader/lexer.h reader/symtab.h
	$(CC) -c $(CFLAGS) reader/lexer.c -o reader/lexer.o

reader/reader.o: common.h math.h program.h ptrarr.h ramp.h reader/file.h reader/reader.c script.h sgensys.h wave.h
	$(CC) -c $(CFLAGS) reader/reader.c -o reader/reader.o

reader/parseconv.o: arrtype.h common.h math.h program.h ptrarr.h ramp.h reader/parseconv.c script.h wave.h
	$(CC) -c $(CFLAGS) reader/parseconv.c -o reader/parseconv.o

reader/parser.o: common.h math.h mempool.h program.h ptrarr.h ramp.h reader/file.h reader/parser.c reader/symtab.h script.h wave.h
	$(CC) -c $(CFLAGS_SIZE) reader/parser.c -o reader/parser.o

reader/scanner.o: common.h math.h mempool.h reader/file.h reader/scanner.c reader/scanner.h reader/symtab.h
	$(CC) -c $(CFLAGS_FAST) reader/scanner.c -o reader/scanner.o

reader/symtab.o: common.h mempool.h reader/symtab.c reader/symtab.h
	$(CC) -c $(CFLAGS_FAST) reader/symtab.c -o reader/symtab.o

renderer/generator.o: common.h math.h mempool.h program.h ramp.h renderer/generator.c renderer/generator.h renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/generator.c -o renderer/generator.o

renderer/osc.o: common.h math.h renderer/osc.c renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/osc.c -o renderer/osc.o

sgensys.o: common.h help.h math.h program.h ptrarr.h ramp.h sgensys.c sgensys.h wave.h
	$(CC) -c $(CFLAGS) sgensys.c

test-scan.o: common.h math.h program.h ptrarr.h ramp.h reader/file.h reader/lexer.h reader/scanner.h reader/symtab.h sgensys.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
