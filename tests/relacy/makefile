# ©2014 Cameron Desrochers

include ../../build/makefile.inc

ifeq ($(OS),Windows_NT)
	PLATFORM_OPTS = -static
else
	PLATFORM_OPTS = -lrt
endif

OPTS = -pthread $(PLATFORM_OPTS) -O3 -g

default: freelist$(EXT) spmchash$(EXT)

freelist$(EXT): makefile freelist.cpp
	g++ -std=c++11 -fpermissive $(OPTS) freelist.cpp -o freelist$(EXT)

spmchash$(EXT): makefile spmchash.cpp
	g++ -std=c++11 -fpermissive $(OPTS) spmchash.cpp -o spmchash$(EXT)

#run: freelist$(EXT)
#	freelist$(EXT)
run: spmchash$(EXT)
	spmchash$(EXT)
