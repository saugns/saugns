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

include makedepend

clean:
	rm -f $(OBJ) mgensys

depend makedepend:
	@$(CC) -MM -DMAKEDEPEND *.c | \
		sed 's/\([a-z]*\)\/\.\.\///g' > makedepend; \
	for dir in `ls -d */`; do \
		if [ -n "`ls $$dir*.c 2> /dev/null`" ]; then \
			$(CC) -MM -DMAKEDEPEND $$dir*.c | \
sed "s/\([a-z]*\)\/\.\.\///g; s/\([a-z]*\.o:\)/$${dir%/}\/\1/g" >> makedepend; \
		fi; \
	done

mgensys: $(OBJ) makedepend
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

arrtype.o:
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o:
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/parser.o:
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/postparse.o:
	$(CC) -c $(CFLAGS) builder/postparse.c -o builder/postparse.o

common.o:
	$(CC) -c $(CFLAGS) common.c

help.o:
	$(CC) -c $(CFLAGS) help.c

interp/generator.o:
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

interp/ngen.o:
	$(CC) -c $(CFLAGS_FAST) interp/ngen.c -o interp/ngen.o

interp/osc.o:
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

interp/runalloc.o:
	$(CC) -c $(CFLAGS_FAST) interp/runalloc.c -o interp/runalloc.o

line.o:
	$(CC) -c $(CFLAGS_FAST) line.c

mempool.o:
	$(CC) -c $(CFLAGS) mempool.c

mgensys.o:
	$(CC) -c $(CFLAGS) mgensys.c

noise.o:
	$(CC) -c $(CFLAGS_FAST) noise.c

ptrarr.o:
	$(CC) -c $(CFLAGS) ptrarr.c

reader/file.o:
	$(CC) -c $(CFLAGS) reader/file.c -o reader/file.o

reader/symtab.o:
	$(CC) -c $(CFLAGS) reader/symtab.c -o reader/symtab.o

player/audiodev.o: player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o:
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/sndfile.o:
	$(CC) -c $(CFLAGS) player/sndfile.c -o player/sndfile.o

wave.o:
	$(CC) -c $(CFLAGS_FAST) wave.c
