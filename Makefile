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
BIN=saugns
MAN1=saugns.1
README=README.md
SHARE=saugns
OBJ=\
	common.o \
	help.o \
	arrtype.o \
	math.o \
	ptrarr.o \
	reader/file.o \
	reader/symtab.o \
	reader/scanner.o \
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
	saugns.o
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
	./$(BIN) -c $(ARGS) */*.sau examples/*/*.sau examples/*/*/*.sau
fullcheck: $(BIN)
	./$(BIN) -m $(ARGS) */*.sau examples/*/*.sau examples/*/*/*.sau
tests: test-scan
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST1_OBJ) test-scan
install: all
	@if [ -d "$(DESTDIR)$(PREFIX)/man" ]; then \
		MANDIR="man"; \
	else \
		MANDIR="share/man"; \
	fi; \
	echo "Installing man page under $(DESTDIR)$(PREFIX)/$$MANDIR."; \
	if [ -d "/usr/share/man" ]; then \
		for f in "/usr/share/man/man"?/*.gz; do \
			[ -e "$$f" ] && USE_GZIP=yes || USE_GZIP=; break; \
		done; \
	else \
		for f in "$(DESTDIR)$(PREFIX)/$$MANDIR/man"?/*.gz; do \
			[ -e "$$f" ] && USE_GZIP=yes || USE_GZIP=; break; \
		done; \
	fi; \
	mkdir -p $(DESTDIR)$(PREFIX)/$$MANDIR/man1; \
	if [ $$USE_GZIP ]; then \
		echo "(Gzip man pages? 'Yes', quick look says.)"; \
		gzip < man/$(MAN1) > $(DESTDIR)$(PREFIX)/$$MANDIR/man1/$(MAN1).gz; \
	else \
		echo "(Gzip man pages? 'No', quick look says.)"; \
		cp -f man/$(MAN1) $(DESTDIR)$(PREFIX)/$$MANDIR/man1; \
	fi; \
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/$(SHARE)
	mkdir -p $(DESTDIR)$(PREFIX)/share/examples/$(SHARE)
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	cp -RP $(README) doc/* $(DESTDIR)$(PREFIX)/share/doc/$(SHARE)
	cp -RP examples/* $(DESTDIR)$(PREFIX)/share/examples/$(SHARE)
uninstall:
	@if [ -d "$(DESTDIR)$(PREFIX)/man" ]; then \
		MANDIR="man"; \
	else \
		MANDIR="share/man"; \
	fi; \
	echo "Uninstalling man page under $(DESTDIR)$(PREFIX)/$$MANDIR."; \
	rm -f $(DESTDIR)$(PREFIX)/$$MANDIR/man1/$(MAN1).gz \
		$(DESTDIR)$(PREFIX)/$$MANDIR/man1/$(MAN1)
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -Rf $(DESTDIR)$(PREFIX)/share/doc/$(SHARE)
	rm -Rf $(DESTDIR)$(PREFIX)/share/examples/$(SHARE)

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

help.o: common.h help.c help.h math.h ramp.h wave.h
	$(CC) -c $(CFLAGS_SIZE) help.c

math.o: common.h math.c math.h
	$(CC) -c $(CFLAGS_FAST) math.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrarr.h ramp.h saugns.h wave.h
	$(CC) -c $(CFLAGS) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

reader/file.o: common.h reader/file.c reader/file.h
	$(CC) -c $(CFLAGS) reader/file.c -o reader/file.o

reader/lexer.o: common.h math.h mempool.h reader/file.h reader/lexer.c reader/lexer.h reader/scanner.h reader/symtab.h
	$(CC) -c $(CFLAGS) reader/lexer.c -o reader/lexer.o

reader/reader.o: common.h math.h program.h ptrarr.h ramp.h reader/file.h reader/reader.c reader/scanner.h reader/script.h saugns.h wave.h
	$(CC) -c $(CFLAGS) reader/reader.c -o reader/reader.o

reader/parseconv.o: arrtype.h common.h math.h program.h ptrarr.h ramp.h reader/parseconv.c reader/script.h wave.h
	$(CC) -c $(CFLAGS) reader/parseconv.c -o reader/parseconv.o

reader/parser.o: common.h math.h mempool.h program.h ramp.h reader/file.h reader/parser.c reader/scanner.h reader/script.h reader/symtab.h wave.h
	$(CC) -c $(CFLAGS_SIZE) reader/parser.c -o reader/parser.o

reader/scanner.o: common.h math.h mempool.h reader/file.h reader/scanner.c reader/scanner.h reader/symtab.h
	$(CC) -c $(CFLAGS_FAST) reader/scanner.c -o reader/scanner.o

reader/symtab.o: common.h mempool.h reader/symtab.c reader/symtab.h
	$(CC) -c $(CFLAGS_FAST) reader/symtab.c -o reader/symtab.o

renderer/generator.o: common.h math.h mempool.h program.h ramp.h renderer/generator.c renderer/generator.h renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/generator.c -o renderer/generator.o

renderer/osc.o: common.h math.h renderer/osc.c renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/osc.c -o renderer/osc.o

saugns.o: common.h help.h math.h program.h ptrarr.h ramp.h saugns.c saugns.h wave.h
	$(CC) -c $(CFLAGS) saugns.c

test-scan.o: common.h math.h program.h ptrarr.h ramp.h reader/file.h reader/lexer.h reader/scanner.h reader/symtab.h saugns.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
