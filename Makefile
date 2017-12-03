CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_UNIX=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    generator.o \
    osc.o \
    parser.o \
    program.o \
    sgensys.o \
    symtab.o \
    wavfile.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	@if [ `uname -s` == 'Linux' ]; then \
		echo "Linking for Linux build (ALSA and OSS)."; \
		$(CC) $(LFLAGS_LINUX) $(OBJ) -o sgensys; \
	else \
		echo "Linking for generic OSS build."; \
		$(CC) $(LFLAGS_UNIX) $(OBJ) -o sgensys; \
	fi

audiodev.o: audiodev.c audiodev_*.c audiodev.h
	$(CC) -c $(CFLAGS) audiodev.c

generator.o: generator.c generator.h math.h osc.h program.h
	$(CC) -c $(CFLAGS) generator.c

osc.o: osc.c osc.h math.h
	$(CC) -c $(CFLAGS) osc.c

parser.o: parser.c parser.h program.h symtab.h math.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c parser.h program.h
	$(CC) -c $(CFLAGS) program.c

sgensys.o: sgensys.c audiodev.h wavfile.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c symtab.h
	$(CC) -c $(CFLAGS) symtab.c

wavfile.o: wavfile.c wavfile.h
	$(CC) -c $(CFLAGS) wavfile.c
