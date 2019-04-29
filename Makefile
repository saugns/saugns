CFLAGS_COMMON=-std=c99 -W -Wall
CFLAGS=$(CFLAGS_COMMON) -O2
CFLAGS_FAST=$(CFLAGS_COMMON) -O3 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=error.o \
    arrtype.o \
    symtab.o \
    lexer.o \
    parser.o \
    parseconv.o \
    mempool.o \
    ramp.o \
    wave.o \
    generator.o \
    audiodev.o \
    wavfile.o \
    sgensys.o
BIN=sgensys

all: $(BIN)

check: $(BIN)
	for f in */*.sgs examples/*/*.sgs; do \
		./$(BIN) -c $$f; \
	done

clean:
	rm -f $(OBJ) $(BIN)

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

arrtype.o: arrtype.c arrtype.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

error.o: error.c sgensys.h
	$(CC) -c $(CFLAGS) error.c

lexer.o: lexer.c lexer.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

parseconv.o: parseconv.c program.h ramp.h wave.h math.h script.h mempool.h arrtype.h sgensys.h
	$(CC) -c $(CFLAGS) parseconv.c

parser.o: parser.c script.h mempool.h symtab.h program.h ramp.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

symtab.o: symtab.c symtab.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

mempool.o: mempool.c mempool.h sgensys.h
	$(CC) -c $(CFLAGS) mempool.c

ramp.o: ramp.c ramp.h math.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

generator.o: generator.c generator.h osc.h program.h ramp.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) generator.c

sgensys.o: sgensys.c lexer.h script.h generator.h program.h ramp.h wave.h math.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

wave.o: wave.c wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS_FAST) wave.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
