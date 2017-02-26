/*
 * Hello concurrency example from samples.md
 */

#include <moodycamel/concurrentqueue.h>
#include <cassert>
#include <thread>

using namespace moodycamel;

int main()
{
    ConcurrentQueue<int> q;
    int dequeued[100] = { 0 };
    std::thread threads[20];

    // Producers
    for (int i = 0; i != 10; ++i) {
        threads[i] = std::thread([&](int i) {
            for (int j = 0; j != 10; ++j) {
                q.enqueue(i * 10 + j);
            }
        }, i);
    }

    // Consumers
    for (int i = 10; i != 20; ++i) {
        threads[i] = std::thread([&]() {
            int item;
            for (int j = 0; j != 20; ++j) {
                if (q.try_dequeue(item)) {
                    ++dequeued[item];
                }
            }
        });
    }

    // Wait for all threads
    for (int i = 0; i != 20; ++i) {
        threads[i].join();
    }

    // Collect any leftovers (could be some if e.g. consumers finish before producers)
    int item;
    while (q.try_dequeue(item)) {
        ++dequeued[item];
    }

    // Make sure everything went in and came back out!
    for (int i = 0; i != 100; ++i) {
        assert(dequeued[i] == 1);
    }
}
