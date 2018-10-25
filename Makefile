CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=common.o \
    arrtype.o \
    cbuf.o \
    ptrlist.o \
    builder/symtab.o \
    builder/file.o \
    builder/lexer.o \
    builder/parser.o \
    builder.o \
    mempool.o \
    program/parseconv.o \
    program/slope.o \
    program/wave.o \
    renderer.o \
    renderer/generator.o \
    audiodev.o \
    wavfile.o \
    ssndgen.o

all: ssndgen

clean:
	rm -f $(OBJ) ssndgen

ssndgen: $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o ssndgen; \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o ssndgen; \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o ssndgen; \
	else \
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o ssndgen; \
	fi

# Headers

audiodev.h: common.h
script.h: program.h ptrlist.h
math.h: common.h
program.h: program/slope.h program/wave.h
program/slope.h: common.h
program/wave.h: common.h
renderer/osc.h: math.h program/wave.h
wavfile.h: common.h

# Objects

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h
	$(CC) -c $(CFLAGS) audiodev.c

cbuf.o: cbuf.c cbuf.h common.h
	$(CC) -c $(CFLAGS) cbuf.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c ssndgen.h builder/lexer.h script.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h cbuf.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/symtab.h builder/file.h cbuf.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parser.o: builder/parser.c script.h builder/symtab.h builder/file.h cbuf.h math.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h common.h
	$(CC) -c $(CFLAGS) mempool.c

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

renderer.o: renderer.c ssndgen.h renderer/generator.h program.h audiodev.h wavfile.h
	$(CC) -c $(CFLAGS) renderer.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h
	$(CC) -c $(CFLAGS) renderer/generator.c -o renderer/generator.o

ssndgen.o: ssndgen.c ssndgen.h program.h
	$(CC) -c $(CFLAGS) ssndgen.c

program/parseconv.o: program/parseconv.c script.h
	$(CC) -c $(CFLAGS) program/parseconv.c -o program/parseconv.o

program/slope.o: program/slope.c program/slope.h math.h
	$(CC) -c $(CFLAGS) program/slope.c -o program/slope.o

program/wave.o: program/wave.c program/wave.h math.h
	$(CC) -c $(CFLAGS) program/wave.c -o program/wave.o

wavfile.o: wavfile.c wavfile.h
	$(CC) -c $(CFLAGS) wavfile.c
