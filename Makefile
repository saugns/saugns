CFLAGS=-O2 -ffast-math
LFLAGS=-s
OBJ=mgensys.o parser.o generator.o

all: mgensys

clean:
	rm -f $(OBJ) mgensys

mgensys: $(OBJ)
	$(CC) $(LFLAGS) $(OBJ) -o mgensys

mgensys.o: mgensys.c mgensys.h
	$(CC) -c $(CFLAGS) mgensys.c

parser.o: parser.c mgensys.h program.h
	$(CC) -c $(CFLAGS) parser.c

generator.o: generator.c mgensys.h program.h osc.h
	$(CC) -c $(CFLAGS) generator.c
