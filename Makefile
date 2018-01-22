CFLAGS=-W -Wall -O2 -ffast-math
LFLAGS=-s -lm
OBJ=sgensys.o parser.o program.o symtab.o generator.o osc.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	$(CC) $(LFLAGS) $(OBJ) -o sgensys

sgensys.o: sgensys.c sgensys.h
	$(CC) -c $(CFLAGS) sgensys.c

parser.o: parser.c sgensys.h parser.h program.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c sgensys.h parser.h program.h
	$(CC) -c $(CFLAGS) program.c

symtab.o: symtab.c sgensys.h symtab.h
	$(CC) -c $(CFLAGS) symtab.c

generator.o: generator.c sgensys.h program.h osc.h
	$(CC) -c $(CFLAGS) generator.c

osc.o: osc.c osc.h
	$(CC) -c $(CFLAGS) osc.c
