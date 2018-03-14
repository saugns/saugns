CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=\
	common.o \
	arrtype.o \
	builder/ptrlist.o \
	builder/symtab.o \
	builder/file.o \
	builder/parser.o \
	builder/parseconv.o \
	builder.o \
	mempool.o \
	program/slope.o \
	program/wave.o \
	renderer.o \
	renderer/generator.o \
	audiodev.o \
	wavfile.o \
	sgensys.o
TEST_OBJ=\
	common.o \
	arrtype.o \
	builder/ptrlist.o \
	builder/symtab.o \
	builder/file.o \
	builder/scanner.o \
	builder/lexer.o \
	builder/parseconv.o \
	mempool.o \
	test-builder.o

all: sgensys
test: test-builder
clean:
	rm -f $(OBJ) sgensys
	rm -f $(TEST_OBJ) test-builder

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

test-builder: $(TEST_OBJ)
	$(CC) $(TEST_OBJ) $(LFLAGS) -o test-builder

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h common.h
	$(CC) -c $(CFLAGS) audiodev.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c sgensys.h builder/script.h builder/ptrlist.h program.h program/param.h program/slope.h program/wave.h common.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/symtab.h builder/file.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c builder/script.h builder/ptrlist.h program.h program/param.h program/slope.h program/wave.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c builder/script.h builder/ptrlist.h program.h program/param.h program/slope.h program/wave.h builder/symtab.h builder/file.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/ptrlist.o: builder/ptrlist.c builder/ptrlist.h common.h
	$(CC) -c $(CFLAGS) builder/ptrlist.c -o builder/ptrlist.o

builder/scanner.o: builder/scanner.c builder/scanner.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scanner.c -o builder/scanner.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h arrtype.h common.h
	$(CC) -c $(CFLAGS) mempool.c

program/slope.o: program/slope.c program/slope.h math.h common.h
	$(CC) -c $(CFLAGS) program/slope.c -o program/slope.o

program/wave.o: program/wave.c program/wave.h math.h common.h
	$(CC) -c $(CFLAGS) program/wave.c -o program/wave.o

renderer.o: renderer.c sgensys.h renderer/generator.h program.h program/param.h program/slope.h program/wave.h audiodev.h wavfile.h common.h
	$(CC) -c $(CFLAGS) renderer.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h program/param.h program/slope.h program/wave.h math.h common.h
	$(CC) -c $(CFLAGS) renderer/generator.c -o renderer/generator.o

sgensys.o: sgensys.c sgensys.h program.h program/param.h program/slope.h program/wave.h common.h
	$(CC) -c $(CFLAGS) sgensys.c

test-builder.o: test-builder.c sgensys.h program.h program/param.h program/slope.h program/wave.h builder/scanner.h builder/file.h builder/lexer.h builder/symtab.h common.h
	$(CC) -c $(CFLAGS) test-builder.c

wavfile.o: wavfile.c wavfile.h common.h
	$(CC) -c $(CFLAGS) wavfile.c
