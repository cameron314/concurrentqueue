# ©2013-2014 Cameron Desrochers
# Makefile to build main tests and benchmarks, suitable
# for use on unixen and Windows with MinGW + GnuWin32

include makefile.inc

BASE_OPTS = -pthread

DEBUG_OPTS = -g -O0   # -DMOODYCAMEL_QUEUE_INTERNAL_DEBUG
RELEASE_OPTS = -O2 -g -DNDEBUG

ifeq ($(OS),Windows_NT)
	PLATFORM_OPTS = -static
	TBB_PLATFORM_OPTS = -DUSE_WINTHREAD
else
	LD_PLATFORM_OPTS = -lrt
	# -fsanitize=address seems to have a slow memory leak when creating/destroying a lot of threads
	#DEBUG_OPTS += -fno-omit-frame-pointer -fsanitize=address
endif

OPTS = $(BASE_OPTS) $(PLATFORM_OPTS) $(DEBUG_OPTS)
BENCH_OPTS = $(BASE_OPTS) $(PLATFORM_OPTS) $(RELEASE_OPTS)
LD_OPTS = $(LD_PLATFORM_OPTS)


default: tests benchmarks

tests: bin/unittests$(EXT) bin/fuzztests$(EXT)
	
benchmarks: bin/benchmarks$(EXT)

bin/unittests$(EXT): ../concurrentqueue.h ../blockingconcurrentqueue.h ../tests/unittests/unittests.cpp ../tests/unittests/mallocmacro.cpp ../tests/common/simplethread.h ../tests/common/simplethread.cpp ../tests/common/systemtime.h ../tests/common/systemtime.cpp ../tests/corealgos.h ../tests/unittests/minitest.h makefile
	test -d bin || mkdir bin
	g++ -std=c++11 -Wall -pedantic-errors -Wpedantic -Wconversion $(OPTS) -fno-elide-constructors ../tests/common/simplethread.cpp ../tests/common/systemtime.cpp ../tests/unittests/unittests.cpp -o bin/unittests$(EXT) $(LD_OPTS)

bin/fuzztests$(EXT): ../concurrentqueue.h ../tests/fuzztests/fuzztests.cpp ../tests/common/simplethread.h ../tests/common/simplethread.cpp ../tests/common/systemtime.h ../tests/common/systemtime.cpp ../tests/corealgos.h makefile
	test -d bin || mkdir bin
	g++ -std=c++11 -Wall -pedantic-errors -Wpedantic $(BENCH_OPTS) ../tests/common/simplethread.cpp ../tests/common/systemtime.cpp ../tests/fuzztests/fuzztests.cpp -o bin/fuzztests$(EXT) $(LD_OPTS)

bin/benchmarks$(EXT): bin/libtbb.a ../concurrentqueue.h ../benchmarks/benchmarks.cpp ../benchmarks/cpuid.h ../benchmarks/cpuid.cpp ../benchmarks/lockbasedqueue.h ../benchmarks/simplelockfree.h ../tests/common/simplethread.h ../tests/common/simplethread.cpp ../tests/common/systemtime.h ../tests/common/systemtime.cpp makefile
	test -d bin || mkdir bin
	g++ -std=c++11 -Wall -pedantic-errors -Wpedantic $(BENCH_OPTS) -I../benchmarks ../benchmarks/cpuid.cpp ../tests/common/simplethread.cpp ../tests/common/systemtime.cpp ../benchmarks/benchmarks.cpp -o bin/benchmarks$(EXT) -Lbin -ltbb $(LD_OPTS)
	
bin/libtbb.a: makefile
	test -d bin || mkdir bin
	g++ -std=c++11 -O2 -DNDEBUG -D__TBB_BUILD=1 $(TBB_PLATFORM_OPTS) -I../benchmarks -c ../benchmarks/tbb/cache_aligned_allocator.cpp ../benchmarks/tbb/concurrent_monitor.cpp ../benchmarks/tbb/concurrent_queue.cpp ../benchmarks/tbb/dynamic_link.cpp ../benchmarks/tbb/tbb_misc.cpp
	ar -rc bin/libtbb.a cache_aligned_allocator.o concurrent_monitor.o concurrent_queue.o dynamic_link.o tbb_misc.o
	rm -f cache_aligned_allocator.o concurrent_monitor.o concurrent_queue.o dynamic_link.o tbb_misc.o
