CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
OBJ=sgensys.o parser.o program.o symtab.o generator.o osc.o wavfile.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	$(CC) $(LFLAGS) $(OBJ) -o sgensys

sgensys.o: sgensys.c sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

parser.o: parser.c math.h parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c parser.h program.h sgensys.h
	$(CC) -c $(CFLAGS) program.c

symtab.o: symtab.c sgensys.h symtab.h
	$(CC) -c $(CFLAGS) symtab.c

generator.o: generator.c math.h osc.h program.h sgensys.h
	$(CC) -c $(CFLAGS) generator.c

osc.o: osc.c math.h osc.h sgensys.h 
	$(CC) -c $(CFLAGS) osc.c

wavfile.o: wavfile.c sgensys.h wavfile.h
	$(CC) -c $(CFLAGS) wavfile.c
