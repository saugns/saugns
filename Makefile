CFLAGS=-std=c99 -W -Wall -O2 -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/parser.o \
	builder/parseconv.o \
	builder.o \
	mempool.o \
	slope.o \
	wave.o \
	renderer.o \
	renderer/osc.o \
	renderer/generator.o \
	audiodev.o \
	wavfile.o \
	saugns.o
TEST_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/lexer.o \
	builder/parseconv.o \
	mempool.o \
	test-builder.o

all: saugns
test: test-builder
clean:
	rm -f $(OBJ) saugns
	rm -f $(TEST_OBJ) test-builder

saugns: $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o saugns; \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o saugns; \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o saugns; \
	else \
		echo "Linking for generic UNIX (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS) -o saugns; \
	fi

test-builder: $(TEST_OBJ)
	$(CC) $(TEST_OBJ) $(LFLAGS) -o test-builder

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

audiodev.o: audiodev.c audiodev/*.c audiodev.h common.h
	$(CC) -c $(CFLAGS) audiodev.c

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

builder.o: builder.c saugns.h script.h ptrlist.h program.h slope.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder.c

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/file.h builder/symtab.h builder/scanner.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c program.h slope.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c builder/scanner.h builder/file.h builder/symtab.h script.h ptrlist.h program.h slope.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/scanner.o: builder/scanner.c builder/scanner.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scanner.c -o builder/scanner.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

mempool.o: mempool.c mempool.h arrtype.h common.h
	$(CC) -c $(CFLAGS) mempool.c

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

renderer.o: renderer.c saugns.h renderer/generator.h ptrlist.h program.h slope.h wave.h math.h audiodev.h wavfile.h common.h
	$(CC) -c $(CFLAGS) renderer.c

renderer/generator.o: renderer/generator.c renderer/generator.h renderer/osc.h program.h slope.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) renderer/generator.c -o renderer/generator.o

renderer/osc.o: renderer/osc.c renderer/osc.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) renderer/osc.c -o renderer/osc.o

saugns.o: saugns.c saugns.h ptrlist.h program.h slope.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) saugns.c

slope.o: slope.c slope.h math.h common.h
	$(CC) -c $(CFLAGS) slope.c

test-builder.o: test-builder.c saugns.h builder/lexer.h builder/scanner.h builder/file.h builder/symtab.h ptrlist.h program.h slope.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-builder.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS) wave.c

wavfile.o: wavfile.c wavfile.h common.h
	$(CC) -c $(CFLAGS) wavfile.c
