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
BIN=saugns
MAN1=saugns.1
SHARE=saugns
OBJ=\
	common.o \
	help.o \
	arrtype.o \
	math.o \
	ptrlist.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/parser.o \
	loader/voicegraph.o \
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
	saugns.o
TEST1_OBJ=\
	common.o \
	ptrlist.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/lexer.o \
	mempool.o \
	test-scan.o

all: $(BIN)
check: all
	./$(BIN) -c */*.sau examples/*/*.sau examples/*/*/*.sau
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
	cp -RP doc/* $(DESTDIR)$(PREFIX)/share/doc/$(SHARE)
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

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h math.h ramp.h wave.h
	$(CC) -c $(CFLAGS_SIZE) help.c

loader/file.o: common.h loader/file.c loader/file.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/lexer.o: common.h loader/file.h loader/lexer.c loader/lexer.h loader/symtab.h loader/scanner.h math.h mempool.h
	$(CC) -c $(CFLAGS) loader/lexer.c -o loader/lexer.o

loader/loader.o: common.h loader/loader.c math.h program.h ptrlist.h ramp.h script.h saugns.h wave.h
	$(CC) -c $(CFLAGS) loader/loader.c -o loader/loader.o

loader/parseconv.o: arrtype.h common.h help.h loader/parseconv.c loader/parseconv.h math.h program.h ptrlist.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/parseconv.c -o loader/parseconv.o

loader/parser.o: common.h help.h loader/file.h loader/parser.c loader/scanner.h loader/symtab.h math.h mempool.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS_SIZE) loader/parser.c -o loader/parser.o

loader/scanner.o: common.h loader/file.h loader/scanner.c loader/scanner.h loader/symtab.h math.h mempool.h
	$(CC) -c $(CFLAGS_FAST) loader/scanner.c -o loader/scanner.o

loader/symtab.o: common.h loader/symtab.c loader/symtab.h mempool.h
	$(CC) -c $(CFLAGS_FAST) loader/symtab.c -o loader/symtab.o

loader/voicegraph.o: arrtype.h common.h loader/voicegraph.c loader/parseconv.h math.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/voicegraph.c -o loader/voicegraph.o

math.o: common.h math.c math.h
	$(CC) -c $(CFLAGS_FAST) math.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS_FAST) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/player.o: common.h player/audiodev.h player/player.c player/wavfile.h renderer/generator.h math.h program.h ptrlist.h ramp.h saugns.h wave.h
	$(CC) -c $(CFLAGS) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrlist.o: common.h ptrlist.c ptrlist.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FASTF) ramp.c

renderer/generator.o: common.h math.h mempool.h ramp.h renderer/generator.c renderer/generator.h renderer/mixer.h renderer/osc.h program.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/generator.c -o renderer/generator.o

renderer/mixer.o: common.h math.h ramp.h renderer/mixer.c renderer/mixer.h
	$(CC) -c $(CFLAGS_FASTF) renderer/mixer.c -o renderer/mixer.o

renderer/osc.o: common.h math.h renderer/osc.c renderer/osc.h wave.h
	$(CC) -c $(CFLAGS_FASTF) renderer/osc.c -o renderer/osc.o

saugns.o: common.h help.h math.h program.h ptrlist.h ramp.h saugns.c saugns.h wave.h
	$(CC) -c $(CFLAGS) saugns.c

test-scan.o: common.h loader/file.h loader/lexer.h loader/scanner.h loader/symtab.h math.h program.h ptrlist.h ramp.h saugns.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FASTF) wave.c
