CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    wavfile.o \
    osc.o \
    generator.o \
    mempool.o \
    symtab.o \
    lexer.o \
    parser.o \
    program.o \
    sgensys.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o sgensys; \
	elif [ $$UNAME = 'OpenBSD' ] || [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for OpenBSD or NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o sgensys; \
	else \
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o sgensys; \
	fi

audiodev.o: audiodev.c audiodev_*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

generator.o: generator.c generator.h program.h osc.h math.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

lexer.o: lexer.c symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

mempool.o: mempool.c mempool.h sgensys.h
	$(CC) -c $(CFLAGS) mempool.c

osc.o: osc.c osc.h math.h sgensys.h
	$(CC) -c $(CFLAGS) osc.c

parser.o: parser.c parser.h program.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

sgensys.o: sgensys.c audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c symtab.h mempool.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
