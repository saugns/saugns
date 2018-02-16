CFLAGS=-W -Wall -Werror=implicit-function-declaration -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_UNIX=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    interpreter.o \
    osc.o \
    parser.o \
    program.o \
    ptrarr.o \
    renderer.o \
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

audiodev.o: audiodev.c audiodev_*.c audiodev.h sgensys.h
	$(CC) -c $(CFLAGS) audiodev.c

interpreter.o: interpreter.c program.h interpreter.h sgensys.h
	$(CC) -c $(CFLAGS) interpreter.c

osc.o: osc.c osc.h math.h sgensys.h
	$(CC) -c $(CFLAGS) osc.c

parser.o: parser.c parser_*.c math.h ptrarr.h parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c program.h ptrarr.h parser.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

ptrarr.o: ptrarr.c ptrarr.h sgensys.h
	$(CC) -c $(CFLAGS) ptrarr.c

renderer.o: renderer.c renderer.h math.h osc.h program.h interpreter.h sgensys.h
	$(CC) -c $(CFLAGS) renderer.c

sgensys.o: sgensys.c program.h interpreter.h renderer.h audiodev.h wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c symtab.h sgensys.h
	$(CC) -c $(CFLAGS) symtab.c

wavfile.o: wavfile.c wavfile.h sgensys.h
	$(CC) -c $(CFLAGS) wavfile.c
