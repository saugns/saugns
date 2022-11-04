.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O3
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=reader/file.o reader/symtab.o \
    builder/parser.o builder/postparse.o builder/builder.o \
    player/audiodev.o player/sndfile.o player/player.o \
    interp/generator.o interp/ngen.o interp/osc.o interp/runalloc.o \
    common.o mempool.o ptrarr.o arrtype.o \
    line.o noise.o wave.o help.o \
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

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o: builder/builder.c common.h mgensys.h ptrarr.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/parser.o: builder/parser.c builder/parser.h common.h help.h reader/file.h reader/symtab.h math.h mempool.h mgensys.h noise.h program.h wave.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/postparse.o: builder/parser.h builder/postparse.c common.h help.h reader/file.h reader/symtab.h math.h mempool.h mgensys.h noise.h program.h wave.h
	$(CC) -c $(CFLAGS) builder/postparse.c -o builder/postparse.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h line.h math.h noise.h wave.h
	$(CC) -c $(CFLAGS) help.c

interp/generator.o: arrtype.h common.h interp/generator.c interp/ngen.h interp/osc.h interp/runalloc.h math.h mempool.h mgensys.h noise.h program.h ptrarr.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

interp/ngen.o: common.h interp/ngen.c interp/ngen.h math.h noise.h
	$(CC) -c $(CFLAGS_FAST) interp/ngen.c -o interp/ngen.o

interp/osc.o: common.h interp/osc.c interp/osc.h math.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

interp/runalloc.o: arrtype.h common.h interp/ngen.h interp/osc.h interp/runalloc.c interp/runalloc.h math.h mempool.h mgensys.h noise.h program.h ptrarr.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/runalloc.c -o interp/runalloc.o

line.o: common.h line.c line.h math.h
	$(CC) -c $(CFLAGS_FAST) line.c

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS) mempool.c

mgensys.o: common.h help.h mgensys.c mgensys.h ptrarr.h
	$(CC) -c $(CFLAGS) mgensys.c

noise.o: common.h math.h noise.c noise.h
	$(CC) -c $(CFLAGS_FAST) noise.c

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

reader/file.o: common.h reader/file.c reader/file.h
	$(CC) -c $(CFLAGS) reader/file.c -o reader/file.o

reader/symtab.o: common.h mempool.h mgensys.h reader/symtab.c reader/symtab.h
	$(CC) -c $(CFLAGS) reader/symtab.c -o reader/symtab.o

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o: common.h math.h mgensys.h player/audiodev.h player/player.c player/sndfile.h program.h ptrarr.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/sndfile.o: common.h player/sndfile.c player/sndfile.h
	$(CC) -c $(CFLAGS) player/sndfile.c -o player/sndfile.o

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FAST) wave.c
