.POSIX:
CC=cc
CFLAGS_COMMON=-std=c99 -W -Wall
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=sgensys
SHARE=sgensys
OBJ=\
	arrtype.o \
	error.o \
	file.o \
	symtab.o \
	parser.o \
	mempool.o \
	ramp.o \
	wave.o \
	generator.o \
	audiodev.o \
	wavfile.o \
	sgensys.o
TEST1_OBJ=\
	arrtype.o \
	error.o \
	file.o \
	symtab.o \
	scanner.o \
	lexer.o \
	mempool.o \
	test-scan.o

all: $(BIN)
check: $(BIN)
	for f in */*.sgs examples/*/*.sgs; do \
		./$(BIN) -c $$f; \
	done
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

arrtype.o: arrtype.c arrtype.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

error.o: error.c sgensys.h
	$(CC) -c $(CFLAGS) error.c

file.o: file.c file.h sgensys.h
	$(CC) -c $(CFLAGS) file.c

lexer.o: lexer.c lexer.h file.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

parser.o: parser.c parser/parseconv.h file.h symtab.h script.h mempool.h program.h ramp.h wave.h math.h arrtype.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

scanner.o: scanner.c scanner.h file.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) scanner.c

symtab.o: symtab.c symtab.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

mempool.o: mempool.c mempool.h sgensys.h
	$(CC) -c $(CFLAGS) mempool.c

ramp.o: ramp.c ramp.h math.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

generator.o: generator.c generator.h osc.h program.h ramp.h wave.h math.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) generator.c

sgensys.o: sgensys.c file.h script.h generator.h program.h ramp.h wave.h math.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

test-scan.o: test-scan.c lexer.h scanner.h file.h symtab.h program.h ramp.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: wave.c wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) wave.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
