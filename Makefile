CC= gcc
RM= rm
SOURCES= cd.c
OBJECTS= $(SOURCES:.c=.o)
LDFLAGS=
CFLAGS=-c -Wall

all: ${OBJECTS} 
	${CC} -o gcli ${OBJECTS} ${LDFLAGS}

.o: 
	${CC} ${CFLAGS} $< 

clean:
	${RM} -f *.o gcli