CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=cbuf.o \
    stream.o \
    streamf.o \
    scanner.o \
    garr.o \
    plist.o \
    mempool.o \
    symtab.o \
    lexer.o \
    parser.o \
    program.o \
    wave.o \
    generator.o \
    audiodev.o \
    wavfile.o \
    sgensys.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o sgensys; \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o sgensys; \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o sgensys; \
	else \
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o sgensys; \
	fi

audiodev.o: audiodev.c audiodev/*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

cbuf.o: cbuf.c cbuf.h sgensys.h
	$(CC) -c $(CFLAGS) cbuf.c

garr.o: garr.c garr.h sgensys.h
	$(CC) -c $(CFLAGS) garr.c

generator.o: generator.c generator.h osc.h wave.h math.h program.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

lexer.o: lexer.c lexer.h streamf.h stream.h cbuf.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

mempool.o: mempool.c mempool.h sgensys.h
	$(CC) -c $(CFLAGS) mempool.c

parser.o: parser.c parser.h scanner.h streamf.h stream.h cbuf.h symtab.h mempool.h program.h plist.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

plist.o: plist.c plist.h sgensys.h
	$(CC) -c $(CFLAGS) plist.c

program.o: program.c program.h parser.h garr.h plist.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

scanner.o: scanner.c scanner.h streamf.h stream.h cbuf.h math.h sgensys.h
	$(CC) -c $(CFLAGS) scanner.c

sgensys.o: sgensys.c generator.h program.h parser.h wave.h plist.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

stream.o: stream.c stream.h cbuf.h sgensys.h
	$(CC) -c $(CFLAGS) stream.c

streamf.o: streamf.c streamf.h stream.h cbuf.h sgensys.h
	$(CC) -c $(CFLAGS) streamf.c

symtab.o: symtab.c symtab.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

wave.o: wave.c wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) wave.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
