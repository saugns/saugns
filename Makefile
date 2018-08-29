CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=common.o \
    arrtype.o \
    ptrlist.o \
    builder/symtab.o \
    builder/file.o \
    builder/lexer.o \
    builder/parser.o \
    builder/parseconv.o \
    builder.o \
    mempool.o \
    wave.o \
    renderer.o \
    renderer/generator.o \
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

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h common.h
	$(CC) -c $(CFLAGS) audiodev.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c sgensys.h builder/lexer.h script.h program.h ptrlist.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/symtab.h builder/file.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c program.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c script.h ptrlist.h builder/symtab.h builder/file.h program.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h common.h
	$(CC) -c $(CFLAGS) mempool.c

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

renderer.o: renderer.c sgensys.h renderer/generator.h program.h wave.h math.h audiodev.h wavfile.h common.h
	$(CC) -c $(CFLAGS) renderer.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) renderer/generator.c -o renderer/generator.o

sgensys.o: sgensys.c sgensys.h program.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) sgensys.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS) wave.c

wavfile.o: wavfile.c wavfile.h common.h
	$(CC) -c $(CFLAGS) wavfile.c
