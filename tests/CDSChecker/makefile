default: enqueue_dequeue_one.o enqueue_dequeue_many.o

enqueue_dequeue_one.o: CDSChecker makefile enqueue_dequeue_many.cpp enqueue_dequeue_one.cpp corealgo.h
	g++ -std=c++11 -DNDEBUG -O3 enqueue_dequeue_one.cpp -o enqueue_dequeue_one.o -Lmodel-checker -lmodel

enqueue_dequeue_many.o: CDSChecker makefile enqueue_dequeue_many.cpp corealgo.h
	g++ -std=c++11 -DNDEBUG -O3 enqueue_dequeue_many.cpp -o enqueue_dequeue_many.o -Lmodel-checker -lmodel

CDSChecker:
	$(MAKE) -C model-checker

run: enqueue_dequeue_one.o enqueue_dequeue_many.o
	cd model-checker && ./run.sh ../enqueue_dequeue_one.o > ../enqueue_dequeue_one.log
	cd model-checker && ./run.sh ../enqueue_dequeue_many.o > ../enqueue_dequeue_many.log
