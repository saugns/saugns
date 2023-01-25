.POSIX:
CC=cc
CFLAGS_COMMON=-std=c99 -W -Wall -I.
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3
CFLAGS_FASTF=$(CFLAGS_COMMON) -O3 -ffast-math
CFLAGS_SIZE=$(CFLAGS_COMMON) -Os
LFLAGS=-s -Lsau -lsau -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
LFLAGS_TESTS=-s -Lsau -lsau-tests -lm
PREFIX ?=/usr/local
BIN=saugns
MAN1=saugns.1
README=CHANGELOG.md README.md
SHARE=saugns
OBJ=\
	player/audiodev.o \
	player/sndfile.o \
	saugns.o
TEST1_OBJ=\
	test-scan.o

all: $(BIN)
check: $(BIN)
	./$(BIN) -cd $(ARGS) */*.sau examples/*/*.sau examples/*/*/*.sau
fullcheck: $(BIN)
	./$(BIN) -md $(ARGS) */*.sau examples/*/*.sau examples/*/*/*.sau
tests: test-scan
clean:
	(cd sau; make clean)
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST1_OBJ) test-scan
install: all
	@if [ -d "$(DESTDIR)$(PREFIX)/man" ]; then \
		MANDIR="man"; \
	else \
		MANDIR="share/man"; \
	fi; \
	DOCSDIR="share/doc/$(SHARE)"; \
	if [ -d "$(DESTDIR)$(PREFIX)/share/examples" ]; then \
		EXAMPLESDIR="share/examples/$(SHARE)"; \
	else \
		EXAMPLESDIR="share/$(SHARE)/examples"; \
	fi; \
	echo "Installing binary under $(DESTDIR)$(PREFIX)/bin."; \
	mkdir -p $(DESTDIR)$(PREFIX)/bin; \
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin; \
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
	echo "Installing docs under $(DESTDIR)$(PREFIX)/$$DOCSDIR."; \
	mkdir -p $(DESTDIR)$(PREFIX)/$$DOCSDIR; \
	cp -RP $(README) sau/doc/* $(DESTDIR)$(PREFIX)/$$DOCSDIR; \
	echo "Installing examples under $(DESTDIR)$(PREFIX)/$$EXAMPLESDIR."; \
	mkdir -p $(DESTDIR)$(PREFIX)/$$EXAMPLESDIR; \
	cp -RP examples/* $(DESTDIR)$(PREFIX)/$$EXAMPLESDIR;
uninstall:
	@if [ -d "$(DESTDIR)$(PREFIX)/man" ]; then \
		MANDIR="man"; \
	else \
		MANDIR="share/man"; \
	fi; \
	DOCSDIR="share/doc/$(SHARE)"; \
	DATADIR="share/$(SHARE)"; \
	if [ -d "$(DESTDIR)$(PREFIX)/share/examples/$(SHARE)" ]; then \
		EXAMPLESDIR="share/examples/$(SHARE)"; \
	else \
		EXAMPLESDIR="share/$(SHARE)/examples"; \
	fi; \
	echo "Uninstalling binary under $(DESTDIR)$(PREFIX)/bin."; \
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN); \
	echo "Uninstalling man page under $(DESTDIR)$(PREFIX)/$$MANDIR."; \
	rm -f $(DESTDIR)$(PREFIX)/$$MANDIR/man1/$(MAN1).gz \
		$(DESTDIR)$(PREFIX)/$$MANDIR/man1/$(MAN1); \
	echo "Uninstalling docs under $(DESTDIR)$(PREFIX)/$$DOCSDIR."; \
	rm -Rf $(DESTDIR)$(PREFIX)/$$DOCSDIR; \
	echo "Uninstalling examples under $(DESTDIR)$(PREFIX)/$$EXAMPLESDIR."; \
	rm -Rf $(DESTDIR)$(PREFIX)/$$EXAMPLESDIR $(DESTDIR)$(PREFIX)/$$DATADIR;

$(BIN): $(OBJ) sau/libsau.a
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

sau/libsau.a sau/libsau-tests.a: sau/*.[hc] sau/generator/*.[hc]
	(cd sau; make)

test-scan: $(TEST1_OBJ) sau/libsau-tests.a
	$(CC) $(TEST1_OBJ) $(LFLAGS_TESTS) -o test-scan

player/audiodev.o: player/audiodev.c player/audiodev.h player/audiodev/*.c sau/common.h
	$(CC) -c $(CFLAGS_SIZE) player/audiodev.c -o player/audiodev.o

player/sndfile.o: player/sndfile.c player/sndfile.h sau/common.h
	$(CC) -c $(CFLAGS) player/sndfile.c -o player/sndfile.o

saugns.o: saugns.c saugns.h player/audiodev.h player/sndfile.h sau/common.h sau/help.h sau/generator.h sau/script.h sau/arrtype.h sau/program.h sau/line.h sau/wave.h sau/math.h sau/file.h sau/scanner.h sau/symtab.h
	$(CC) -c $(CFLAGS_SIZE) saugns.c

test-scan.o: test-scan.c saugns.h sau/common.h sau/math.h sau/program.h sau/line.h sau/file.h sau/lexer.h sau/scanner.h sau/symtab.h sau/wave.h
	$(CC) -c $(CFLAGS) test-scan.c
