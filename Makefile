.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=loader/file.o loader/symtab.o \
    builder/parser.o builder/builder.o \
    renderer/audiodev.o renderer/wavfile.o renderer/renderer.o \
    interp/generator.o \
    common.o mempool.o ptrarr.o \
    wave.o help.o \
    mgensys.o

all: mgensys

clean:
	rm -f $(OBJ) mgensys

mgensys: $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o mgensys; \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o mgensys; \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o mgensys; \
	else \
		echo "Linking for UNIX with OSS."; \
		$(CC) $(OBJ) $(LFLAGS) -o mgensys; \
	fi

# Objects...

builder/builder.o: builder/builder.c common.h mgensys.h ptrarr.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/parser.o: builder/parser.c common.h help.h loader/file.h loader/symtab.h mempool.h mgensys.h program.h wave.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h wave.h
	$(CC) -c $(CFLAGS) help.c

interp/generator.o: common.h interp/generator.c interp/osc.h math.h mgensys.h program.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

loader/file.o: common.h loader/file.c loader/file.h
	$(CC) -c $(CFLAGS) loader/file.c -o loader/file.o

loader/symtab.o: common.h loader/symtab.c loader/symtab.h mempool.h mgensys.h
	$(CC) -c $(CFLAGS) loader/symtab.c -o loader/symtab.o

renderer/audiodev.o: common.h renderer/audiodev.c renderer/audiodev.h renderer/audiodev/*.c
	$(CC) -c $(CFLAGS) renderer/audiodev.c -o renderer/audiodev.o

renderer/renderer.o: common.h renderer/audiodev.h renderer/renderer.c renderer/wavfile.h math.h mgensys.h ptrarr.h
	$(CC) -c $(CFLAGS_FAST) renderer/renderer.c -o renderer/renderer.o

renderer/wavfile.o: common.h renderer/wavfile.c renderer/wavfile.h
	$(CC) -c $(CFLAGS) renderer/wavfile.c -o renderer/wavfile.o

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS) mempool.c

mgensys.o: common.h help.h mgensys.c mgensys.h ptrarr.h
	$(CC) -c $(CFLAGS) mgensys.c

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FAST) wave.c
