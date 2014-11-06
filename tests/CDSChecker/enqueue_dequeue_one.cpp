// ©2013 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

#include "model-checker/include/threads.h"
#include "corealgo.h"

void producer_thread(void*)
{
	enqueue(1234);
}

void consumer_thread(void*)
{
	int element;
	bool result = try_dequeue(element);
	MODEL_ASSERT(!result || element == 1234);
	
	if (result) {
		MODEL_ASSERT(!try_dequeue(element));
	}
}

int user_main(int, char**)
{
	init();
	
	thrd_t p, c;
	
	thrd_create(&p, &producer_thread, nullptr);
	thrd_create(&c, &consumer_thread, nullptr);
	
	thrd_join(p);
	thrd_join(c);
	
	return 0;
}
