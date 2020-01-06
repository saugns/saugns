CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    mgensys.o parser.o symtab.o generator.o osc.o

all: mgensys

clean:
	rm -f $(OBJ) mgensys

audiodev.o: audiodev.c audiodev/*.c audiodev.h
	$(CC) -c $(CFLAGS) audiodev.c

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
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o mgensys; \
	fi

mgensys.o: mgensys.c mgensys.h
	$(CC) -c $(CFLAGS) mgensys.c

parser.o: parser.c mgensys.h program.h
	$(CC) -c $(CFLAGS) parser.c

symtab.o: symtab.c mgensys.h symtab.h
	$(CC) -c $(CFLAGS) symtab.c

generator.o: generator.c mgensys.h program.h osc.h
	$(CC) -c $(CFLAGS) generator.c

osc.o: osc.c osc.h
	$(CC) -c $(CFLAGS) osc.c
