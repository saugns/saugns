CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    wavfile.o \
    ptrlist.o \
    mempool.o \
    symtab.o \
    lexer.o \
    parser.o \
    program.o \
    wave.o \
    generator.o \
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

generator.o: generator.c generator.h program.h wave.h math.h osc.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

lexer.o: lexer.c lexer.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

mempool.o: mempool.c mempool.h sgensys.h
	$(CC) -c $(CFLAGS) mempool.c

parser.o: parser.c parser.h ptrlist.h symtab.h program.h wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c program.h wave.h parser.h ptrlist.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

ptrlist.o: ptrlist.c ptrlist.h sgensys.h
	$(CC) -c $(CFLAGS) ptrlist.c

sgensys.o: sgensys.c generator.h program.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c symtab.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

wave.o: wave.c wave.h math.h sgensys.h
	$(CC) -c $(CFLAGS) wave.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
