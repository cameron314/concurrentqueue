TARGET=smr
SRC=smr.cpp
HEADERS=stdafx.h

all: ${TARGET}

${TARGET}: Makefile ${SRC} ${HEADERS}
	g++ ${SRC} -O3 -g -o ${TARGET}

clean:
	rm ${TARGET}
