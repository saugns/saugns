CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
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
		$(CC) $(LFLAGS) $(OBJ) -o sgensys; \
	fi

audiodev.o: audiodev.c audiodev_*.c audiodev.h sgensys.h 
	$(CC) -c $(CFLAGS) audiodev.c

generator.o: generator.c math.h osc.h program.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

osc.o: osc.c math.h osc.h sgensys.h 
	$(CC) -c $(CFLAGS) osc.c

parser.o: parser.c math.h parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

sgensys.o: sgensys.c audiodev.h sgensys.h wavfile.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c sgensys.h symtab.h
	$(CC) -c $(CFLAGS) symtab.c

wavfile.o: wavfile.c sgensys.h wavfile.h
	$(CC) -c $(CFLAGS) wavfile.c
