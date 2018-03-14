CFLAGS=-W -Wall -Werror=implicit-function-declaration -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=ptrarr.o \
    aoalloc.o \
    fread.o \
    symtab.o \
    lexer.o \
    parser.o \
    builder.o \
    wavelut.o \
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
	elif [ $$UNAME = 'OpenBSD' ] || [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for OpenBSD or NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o sgensys; \
	else \
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o sgensys; \
	fi

aoalloc.o: aoalloc.c aoalloc.h sgensys.h
	$(CC) -c $(CFLAGS) aoalloc.c

audiodev.o: audiodev.c audiodev_*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

builder.o: builder.c builder.h program.h parser.h ptrarr.h sgensys.h
	$(CC) -c $(CFLAGS) builder.c

fread.o: fread.c fread.h sgensys.h
	$(CC) -c $(CFLAGS) fread.c

generator.o: generator.c generator.h program.h osc.h wavelut.h math.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

lexer.o: lexer.c lexer.h fread.h symtab.h math.h sgensys.h
	$(CC) -c $(CFLAGS) lexer.c

parser.o: parser.c parser.h fread.h symtab.h program.h ptrarr.h wavelut.h math.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

ptrarr.o: ptrarr.c ptrarr.h sgensys.h
	$(CC) -c $(CFLAGS) ptrarr.c

sgensys.o: sgensys.c generator.h builder.h parser.h program.h wavelut.h ptrarr.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c symtab.h aoalloc.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

wavelut.o: wavelut.c wavelut.h math.h sgensys.h
	$(CC) -c $(CFLAGS) wavelut.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
