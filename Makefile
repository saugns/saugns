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

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o: builder/builder.c saugns.h script.h ptrlist.h program.h nodelist.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/scriptconv.o: builder/scriptconv.c script.h program.h ptrlist.h arrtype.h nodelist.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scriptconv.c -o builder/scriptconv.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

interp/generator.o: interp/generator.c interp/generator.h interp/mixer.h interp/osc.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

interp/mixer.o: interp/mixer.c interp/mixer.h ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) interp/mixer.c -o interp/mixer.o

interp/osc.o: interp/osc.c interp/osc.h wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

loader/file.o: loader/file.c loader/file.h common.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/lexer.o: loader/lexer.c loader/lexer.h loader/file.h loader/symtab.h mempool.h loader/scanner.h math.h common.h
	$(CC) -c $(CFLAGS) loader/lexer.c -o loader/lexer.o

loader/parseconv.o: loader/parseconv.c loader/parser.h mempool.h nodelist.h program.h ramp.h wave.h math.h script.h common.h
	$(CC) -c $(CFLAGS) loader/parseconv.c -o loader/parseconv.o

loader/parser.o: loader/parser.c loader/scanner.h loader/symtab.h loader/parser.h loader/file.h mempool.h script.h nodelist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) loader/parser.c -o loader/parser.o

loader/scanner.o: loader/scanner.c loader/scanner.h math.h loader/file.h loader/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) loader/scanner.c -o loader/scanner.o

loader/symtab.o: loader/symtab.c loader/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) loader/symtab.c -o loader/symtab.o

mempool.o: mempool.c mempool.h arrtype.h common.h
	$(CC) -c $(CFLAGS) mempool.c

nodelist.o: nodelist.c nodelist.h mempool.h common.h
	$(CC) -c $(CFLAGS) nodelist.c

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: ramp.c ramp.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

renderer/audiodev.o: renderer/audiodev.c renderer/audiodev/*.c renderer/audiodev.h common.h
	$(CC) -c $(CFLAGS) renderer/audiodev.c -o renderer/audiodev.o

renderer/renderer.o: renderer/renderer.c saugns.h renderer/audiodev.h renderer/wavfile.h interp/generator.h math.h ptrlist.h program.h ramp.h wave.h common.h
	$(CC) -c $(CFLAGS_FAST) renderer/renderer.c -o renderer/renderer.o

renderer/wavfile.o: renderer/wavfile.c renderer/wavfile.h common.h
	$(CC) -c $(CFLAGS) renderer/wavfile.c -o renderer/wavfile.o

saugns.o: saugns.c saugns.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) saugns.c

test-scan.o: test-scan.c saugns.h loader/lexer.h loader/scanner.h loader/file.h loader/symtab.h mempool.h ptrlist.h program.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c
