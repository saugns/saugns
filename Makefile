.POSIX:
CC=cc
CFLAGS=-std=c99 -pedantic -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
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
	arrtype.o \
	ptrlist.o \
	mempool.o \
	nodelist.o \
	ramp.o \
	wave.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/parser.o \
	loader/parseconv.o \
	builder/voicegraph.o \
	builder/scriptconv.o \
	builder/builder.o \
	interp/osc.o \
	interp/mixer.o \
	interp/generator.o \
	renderer/audiodev.o \
	renderer/wavfile.o \
	renderer/renderer.o \
	saugns.o
TEST1_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	mempool.o \
	loader/file.o \
	loader/symtab.o \
	loader/scanner.o \
	loader/lexer.o \
	builder/voicegraph.o \
	builder/scriptconv.o \
	test-scan.o

all: $(BIN)
tests: test-scan
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST1_OBJ) test-scan
install: $(BIN)
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

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o: arrtype.h builder/builder.c common.h math.h nodelist.h program.h ramp.h saugns.h script.h wave.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/scriptconv.o: arrtype.h builder/scriptconv.c builder/scriptconv.h common.h math.h mempool.h nodelist.h program.h ptrlist.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) builder/scriptconv.c -o builder/scriptconv.o

builder/voicegraph.o: arrtype.h builder/scriptconv.h builder/voicegraph.c common.h math.h mempool.h nodelist.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) builder/voicegraph.c -o builder/voicegraph.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

interp/generator.o: common.h interp/generator.c interp/generator.h interp/mixer.h interp/osc.h math.h program.h ramp.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

interp/mixer.o: common.h interp/mixer.c interp/mixer.h math.h ramp.h
	$(CC) -c $(CFLAGS_FAST) interp/mixer.c -o interp/mixer.o

interp/osc.o: common.h interp/osc.c interp/osc.h math.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

loader/file.o: common.h loader/file.c loader/file.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/lexer.o: common.h loader/file.h loader/lexer.c loader/lexer.h loader/scanner.h loader/symtab.h math.h mempool.h
	$(CC) -c $(CFLAGS) loader/lexer.c -o loader/lexer.o

loader/parseconv.o: common.h loader/parseconv.c loader/parser.h math.h mempool.h nodelist.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/parseconv.c -o loader/parseconv.o

loader/parser.o: common.h loader/file.h loader/parser.c loader/parser.h loader/scanner.h loader/symtab.h math.h mempool.h nodelist.h program.h ramp.h script.h wave.h
	$(CC) -c $(CFLAGS) loader/parser.c -o loader/parser.o

loader/scanner.o: common.h loader/file.h loader/scanner.c loader/scanner.h loader/symtab.h math.h mempool.h
	$(CC) -c $(CFLAGS) loader/scanner.c -o loader/scanner.o

loader/symtab.o: common.h loader/symtab.c loader/symtab.h mempool.h
	$(CC) -c $(CFLAGS) loader/symtab.c -o loader/symtab.o

mempool.o: arrtype.h common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS) mempool.c

nodelist.o: common.h mempool.h nodelist.c nodelist.h
	$(CC) -c $(CFLAGS) nodelist.c

ptrlist.o: common.h mempool.h ptrlist.c ptrlist.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: common.h math.h ramp.c ramp.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

renderer/audiodev.o: common.h renderer/audiodev.c renderer/audiodev.h renderer/audiodev/*.c
	$(CC) -c $(CFLAGS) renderer/audiodev.c -o renderer/audiodev.o

renderer/renderer.o: common.h interp/generator.h renderer/audiodev.h renderer/renderer.c renderer/wavfile.h math.h program.h ptrlist.h ramp.h saugns.h wave.h
	$(CC) -c $(CFLAGS_FAST) renderer/renderer.c -o renderer/renderer.o

renderer/wavfile.o: common.h renderer/wavfile.c renderer/wavfile.h
	$(CC) -c $(CFLAGS) renderer/wavfile.c -o renderer/wavfile.o

saugns.o: common.h saugns.c saugns.h ptrlist.h program.h ramp.h wave.h math.h
	$(CC) -c $(CFLAGS) saugns.c

test-scan.o: common.h loader/file.h loader/lexer.h loader/scanner.h loader/symtab.h math.h mempool.h program.h ptrlist.h ramp.h saugns.h test-scan.c wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FAST) wave.c
