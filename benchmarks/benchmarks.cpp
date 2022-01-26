// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

// Benchmarks for moodycamel::ConcurrentQueue.
// Provides comparative timings of various operations under
// highly artificial circumstances. You've been warned :-)

#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <ctime>
#include <random>
#include <vector>
#include <map>
#include <cassert>
#include <thread>
#include <algorithm>
#include <cctype>

#include "../blockingconcurrentqueue.h"
#include "lockbasedqueue.h"
#include "simplelockfree.h"
#include "boostqueue.h"
#include "tbbqueue.h"
#include "stdqueue.h"
#include "dlibqueue.h"
#include "../tests/common/simplethread.h"
#include "../tests/common/systemtime.h"
#include "cpuid.h"

using namespace moodycamel;


typedef std::minstd_rand RNG_t;

bool precise = false;


enum benchmark_type_t
{
	bench_balanced,
	bench_only_enqueue,
	bench_only_enqueue_prealloc,
	bench_only_enqueue_bulk,
	bench_only_enqueue_bulk_prealloc,
	bench_only_dequeue,
	bench_only_dequeue_bulk,
	bench_mostly_enqueue,
	bench_mostly_enqueue_bulk,
	bench_mostly_dequeue,
	bench_mostly_dequeue_bulk,
	bench_spmc,
	bench_spmc_preproduced,
	bench_mpsc,
	bench_empty_dequeue,
	bench_enqueue_dequeue_pairs,
	bench_heavy_concurrent,
	
	BENCHMARK_TYPE_COUNT
};

const char BENCHMARK_SHORT_NAMES[BENCHMARK_TYPE_COUNT][32] = {
	"balanced",
	"only_enqueue",
	"only_enqueue_prealloc",
	"only_enqueue_bulk",
	"only_enqueue_bulk_prealloc",
	"only_dequeue",
	"only_dequeue_bulk",
	"mostly_enqueue",
	"mostly_enqueue_bulk",
	"mostly_dequeue",
	"mostly_dequeue_bulk",
	"spmc",
	"spmc_preproduced",
	"mpsc",
	"empty_dequeue",
	"enqueue_dequeue_pairs",
	"heavy_concurrent"
};

const char BENCHMARK_NAMES[BENCHMARK_TYPE_COUNT][64] = {
	"balanced",
	"only enqueue",
	"only enqueue (pre-allocated)",
	"only enqueue bulk",
	"only enqueue bulk (pre-allocated)",
	"only dequeue",
	"only dequeue bulk",
	"mostly enqueue",
	"mostly enqueue bulk",
	"mostly dequeue",
	"mostly dequeue bulk",
	"single-producer, multi-consumer",
	"single-producer, multi-consumer (pre-produced)",
	"multi-producer, single-consumer",
	"dequeue from empty",
	"enqueue-dequeue pairs",
	"heavy concurrent"
};

const char BENCHMARK_DESCS[BENCHMARK_TYPE_COUNT][256] = {
	"Measures the average operation speed with multiple symmetrical threads\n  under reasonable load -- small random intervals between accesses",
	"Measures the average operation speed when all threads are producers",
	"Measures the average operation speed when all threads are producers,\n  and the queue has been stretched out first",
	"Measures the average speed of enqueueing an item in bulk when all threads are producers",
	"Measures the average speed of enqueueing an item in bulk when all threads are producers,\n  and the queue has been stretched out first",
	"Measures the average operation speed when all threads are consumers",
	"Measures the average speed of dequeueing an item in bulk when all threads are consumers",
	"Measures the average operation speed when most threads are enqueueing",
	"Measures the average speed of enqueueing an item in bulk under light contention",
	"Measures the average operation speed when most threads are dequeueing",
	"Measures the average speed of dequeueing an item in bulk under light contention",
	"Measures the average speed of dequeueing with only one producer, but multiple consumers",
	"Measures the average speed of dequeueing from a queue pre-filled by one thread",
	"Measures the average speed of dequeueing with only one consumer, but multiple producers",
	"Measures the average speed of attempting to dequeue from an empty queue\n  (that eight separate threads had at one point enqueued to)",
	"Measures the average operation speed with each thread doing an enqueue\n  followed by a dequeue",
	"Measures the average operation speed with many threads under heavy load"
};

const char BENCHMARK_SINGLE_THREAD_NOTES[BENCHMARK_TYPE_COUNT][256] = {
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"No contention -- measures raw failed dequeue speed on empty queue",
	"No contention -- measures speed of immediately dequeueing the item that was just enqueued",
	""
};

int BENCHMARK_THREADS_MEASURED[BENCHMARK_TYPE_COUNT] = {
	0,	// measures nthreads
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	-1,	// nthreads - 1
	0,
	1,	// 1
	0,
	0,
	0,
};

int BENCHMARK_THREADS[BENCHMARK_TYPE_COUNT][9] = {
	{ 2, 3, 4,  8, 12, 16, 32,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 2, 4, 8, 32,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 32,  0,  0,  0,  0, 0 },
	{ 2, 4, 8,  0,  0,  0,  0,  0, 0 },
	{ 2, 4, 8,  0,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 16,  0,  0,  0,  0, 0 },
	{ 1, 3, 7, 15,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 16,  0,  0,  0,  0, 0 },
	{ 1, 2, 8, 32,  0,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 2, 3, 4,  8, 12, 16, 32, 48, 0 },
};

enum queue_id_t
{
	queue_moodycamel_ConcurrentQueue,
	queue_moodycamel_BlockingConcurrentQueue,
	queue_boost,
	queue_tbb,
	queue_simplelockfree,
	queue_lockbased,
	queue_std,
	queue_dlib,
	QUEUE_COUNT
};

const char QUEUE_NAMES[QUEUE_COUNT][64] = {
	"moodycamel::ConcurrentQueue",
	"moodycamel::BlockingConcurrentQueue",
	"boost::lockfree::queue",
	"tbb::concurrent_queue",
	"SimpleLockFreeQueue",
	"LockBasedQueue",
	"std::queue",
	"dlib::pipe"
};

const char QUEUE_SUMMARY_NOTES[QUEUE_COUNT][128] = {
	"including bulk",
	"including bulk",
	"",
	"",
	"",
	"",
	"single thread only",
	""
};

const bool QUEUE_TOKEN_SUPPORT[QUEUE_COUNT] = {
	true,
	true,
	false,
	false,
	false,
	false,
	false,
	false
};

const int QUEUE_MAX_THREADS[QUEUE_COUNT] = {
	-1,		// no limit
	-1,
	-1,
	-1,
	-1,
	-1,
	1,
	-1
};

const bool QUEUE_BENCH_SUPPORT[QUEUE_COUNT][BENCHMARK_TYPE_COUNT] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1 },
	{ 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1 },
	{ 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
	{ 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1 }
};


struct Traits : public moodycamel::ConcurrentQueueDefaultTraits
{
	// Use a slightly larger default block size; the default offers
	// a good trade off between speed and memory usage, but a bigger
	// block size will improve throughput (which is mostly what
	// we're after with these benchmarks).
	static const size_t BLOCK_SIZE = 64;

	// Reuse blocks once allocated.
	static const bool RECYCLE_ALLOCATED_BLOCKS = true;
};


typedef std::uint64_t counter_t;

const counter_t BULK_BATCH_SIZE = 2300;

struct BenchmarkResult
{
	double elapsedTime;
	counter_t operations;
	
	inline bool operator<(BenchmarkResult const& other) const
	{
		return elapsedTime < other.elapsedTime;
	}
};


template<typename TFunc>
counter_t rampUpToMeasurableNumberOfMaxOps(TFunc const& func, counter_t startOps = 256)
{
	counter_t ops = startOps;
	double time;
	do {
		time = func(ops);
		ops *= 2;
	} while (time < (precise ? 30 : 10));
#ifdef NDEBUG
	return ops / 2;
#else
	return ops / 4;
#endif
}

counter_t adjustForThreads(counter_t suggestedOps, int nthreads)
{
	return std::max((counter_t)(suggestedOps / std::pow(2, std::sqrt((nthreads - 1) * 3))), suggestedOps / 16);
}


template<typename TQueue, typename item_t>
counter_t determineMaxOpsForBenchmark(benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int randSeed)
{
	switch (benchmark) {
	case bench_balanced: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([&](counter_t ops) {
			TQueue q;
			RNG_t rng(randSeed * 1);
			std::uniform_int_distribution<int> rand(0, 20);
			double total = 0;
			SystemTime start;
			item_t item = 1;
			for (counter_t i = 0; i != ops; ++i) {
				start = getSystemTime();
				q.enqueue(item);
				total += getTimeDelta(start);
			}
			return total;
		}), nthreads);
	}
	case bench_only_enqueue:
	case bench_only_enqueue_prealloc:
	case bench_mostly_enqueue: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			item_t item = 1;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(item);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_dequeue:
	case bench_mostly_dequeue:
	case bench_spmc:
	case bench_spmc_preproduced:
	case bench_mpsc: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			item_t item = 1;
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(item);
			}
			item_t item_rec;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue(item_rec);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_enqueue_bulk:
	case bench_only_enqueue_bulk_prealloc:
	case bench_mostly_enqueue_bulk: {
		std::vector<item_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([&](counter_t ops) {
			TQueue q;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue_bulk(data.cbegin(), data.size());
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_dequeue_bulk:
	case bench_mostly_dequeue_bulk: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			std::vector<item_t> data(BULK_BATCH_SIZE);
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue_bulk(data.cbegin(), data.size());
			}
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue_bulk(data.begin(), data.size());
			}
			return getTimeDelta(start);
		}), nthreads);
		return 0;
	}
	case bench_empty_dequeue: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			item_t item_rec;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue(item_rec);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_enqueue_dequeue_pairs: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			item_t item = 1;
			item_t item_rec;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(item);
				q.try_dequeue(item_rec);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	
	case bench_heavy_concurrent: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			item_t item=1;
			item_t item_rec;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(item);
				q.try_dequeue(item_rec);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	
	default:
		assert(false && "Every benchmark type must be handled here!");
		return 0;
	}
}


// Returns time elapsed, in (fractional) milliseconds
template<typename TQueue, typename item_t>
double runBenchmark(benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int randSeed, counter_t maxOps, int maxThreads, counter_t& out_opCount)
{
	double result = 0;
	volatile int forceNoOptimizeDummy;
	
	switch (benchmark) {
	case bench_balanced: {
		// Measures the average operation speed with multiple symmetrical threads under reasonable load
		TQueue q;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<counter_t> ops(nthreads);
		std::vector<double> times(nthreads);
		std::atomic<int> ready(0);
		item_t item_rec;
		item_t item = 1;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				SystemTime start;
				RNG_t rng(randSeed * (id + 1));
				std::uniform_int_distribution<int> rand(0, 20);
				ops[id] = 0;
				times[id] = 0;
				typename TQueue::consumer_token_t consTok(q);
				typename TQueue::producer_token_t prodTok(q);

				
				for (counter_t i = 0; i != maxOps; ++i) {
					if (rand(rng) == 0) {
						start = getSystemTime();
						if ((i & 1) == 0) {
							if (useTokens) {
								q.try_dequeue(consTok, item_rec);
							}
							else {
								q.try_dequeue(item_rec);
							}
						}
						else {
							if (useTokens) {
								q.enqueue(prodTok, item);
							}
							else {
								q.enqueue(item);
							}
						}
						times[id] += getTimeDelta(start);
						++ops[id];
					}
				}
			}, tid);
		}
		out_opCount = 0;
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			out_opCount += ops[tid];
			result += times[tid];
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_prealloc: {
		out_opCount = maxOps * nthreads;
		
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		{
			// Enqueue opcount elements first, then dequeue them; this
			// will "stretch out" the queue, letting implementatations
			// that re-use memory internally avoid having to allocate
			// more later during the timed enqueue operations.
			std::vector<SimpleThread> threads(nthreads);
			
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
			
			// Now empty the queue
			
			while (q.try_dequeue(item_rec))
				continue;
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw single-item enqueue speed
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(tok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(item);
				}	
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue: {
		out_opCount = maxOps * nthreads;
		
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		if (nthreads == 1) {
			// No contention -- measures raw single-item enqueue speed
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(tok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(item);
				}	
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_spmc_preproduced:
	case bench_only_dequeue: {
		out_opCount = maxOps * nthreads;
		
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(benchmark == bench_spmc_preproduced ? 1 : nthreads);
			counter_t itemsPerThread = benchmark == bench_spmc_preproduced ? maxOps * nthreads : maxOps;
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](size_t id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != itemsPerThread; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != itemsPerThread; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
			}
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw single-item dequeue speed
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(tok, item_rec);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(item_rec);
				}	
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(tok, item_rec);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(item_rec);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_mostly_enqueue: {
		// Measures the average operation speed when most threads are enqueueing
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		out_opCount = maxOps * nthreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto dequeueThreads = std::max(1, nthreads / 4);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - dequeueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(tok, item);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(item);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - dequeueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(tok, item_rec);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(item_rec);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result += timings[tid];
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_mostly_dequeue: {
		// Measures the average operation speed when most threads are dequeueing
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		out_opCount = maxOps * nthreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto enqueueThreads = std::max(1, nthreads / 4);
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(enqueueThreads);
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid].join();
			}
		}
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - enqueueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(tok, item_rec);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(item_rec);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - enqueueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(tok, item);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(item);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result += timings[tid];
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_bulk_prealloc: {
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		{
			// Enqueue opcount elements first, then dequeue them; this
			// will "stretch out" the queue, letting implementatations
			// that re-use memory internally avoid having to allocate
			// more later during the timed enqueue operations.
			std::vector<SimpleThread> threads(nthreads);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
			
			// Now empty the queue
			while (q.try_dequeue(item_rec))
				continue;
		}
		
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		out_opCount = maxOps * BULK_BATCH_SIZE * nthreads;
		if (nthreads == 1) {
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(tok, data.cbegin(), data.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(data.cbegin(), data.size());
				}	
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_bulk: {
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		out_opCount = maxOps * BULK_BATCH_SIZE * nthreads;
		if (nthreads == 1) {
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(tok, data.cbegin(), data.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(data.cbegin(), data.size());
				}	
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_mostly_enqueue_bulk: {
		// Measures the average speed of enqueueing in bulk under light contention
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto dequeueThreads = std::max(1, nthreads / 4);
		std::vector<counter_t> ops(nthreads - dequeueThreads);
		out_opCount = maxOps * BULK_BATCH_SIZE * (nthreads - dequeueThreads);	// dequeue ops added after
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - dequeueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(tok, data.cbegin(), data.size());
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(data.cbegin(), data.size());
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - dequeueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id, int idBase0) {
				std::vector<int> items(BULK_BATCH_SIZE);
				
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				counter_t totalOps = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(tok, items.begin(), items.size());
						totalOps += actual + (actual == items.size() ? 0 : 1);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(items.begin(), items.size());
						totalOps += actual + (actual == items.size() ? 0 : 1);
					}
				}
				timings[id] = getTimeDelta(start);
				ops[idBase0] = totalOps;
			}, tid, tid - (nthreads - dequeueThreads));
		}
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result += timings[tid];
			if (tid < dequeueThreads) {
				out_opCount += ops[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_only_dequeue_bulk: {
		// Measures the average speed of dequeueing in bulk when all threads are consumers
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		{
			// Fill up the queue first
			std::vector<int> data(BULK_BATCH_SIZE);
			for (int i = 0; i != BULK_BATCH_SIZE; ++i) {
				data[i] = i;
			}
			std::vector<SimpleThread> threads(nthreads);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
		}
		if (nthreads == 1) {
			out_opCount = maxOps * BULK_BATCH_SIZE;
			auto start = getSystemTime();
			std::vector<int> items(BULK_BATCH_SIZE);
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue_bulk(tok, items.begin(), items.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue_bulk(items.begin(), items.size());
				}
			}
			result = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::vector<counter_t> ops(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					std::vector<int> items(BULK_BATCH_SIZE);
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					counter_t totalOps = 0;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							auto actual = q.try_dequeue_bulk(tok, items.begin(), items.size());
							totalOps += actual + (actual == items.size() ? 0 : 1);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							auto actual = q.try_dequeue_bulk(items.begin(), items.size());
							totalOps += actual + (actual == items.size() ? 0 : 1);
						}
					}
					timings[id] = getTimeDelta(start);
					ops[id] = totalOps;
				}, tid);
			}
			result = 0;
			out_opCount = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
				out_opCount += ops[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_mostly_dequeue_bulk: {
		// Measures the average speed of dequeueing in bulk under light contention
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		auto enqueueThreads = std::max(1, nthreads / 4);
		out_opCount = maxOps * BULK_BATCH_SIZE * enqueueThreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		std::vector<counter_t> ops(nthreads - enqueueThreads);
		std::vector<int> enqueueData(BULK_BATCH_SIZE);
		for (int i = 0; i != BULK_BATCH_SIZE; ++i) {
			enqueueData[i] = i;
		}
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(enqueueThreads);
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, enqueueData.cbegin(), enqueueData.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(enqueueData.cbegin(), enqueueData.size());
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid].join();
			}
		}
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - enqueueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				std::vector<int> data(BULK_BATCH_SIZE);
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				counter_t totalOps = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(tok, data.begin(), data.size());
						totalOps += actual + (actual == data.size() ? 0 : 1);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(data.begin(), data.size());
						totalOps += actual + (actual == data.size() ? 0 : 1);
					}
				}
				timings[id] = getTimeDelta(start);
				ops[id] = totalOps;
			}, tid);
		}
		for (int tid = nthreads - enqueueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(tok, enqueueData.cbegin(), enqueueData.size());
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(enqueueData.cbegin(), enqueueData.size());
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result += timings[tid];
			if (tid < nthreads - enqueueThreads) {
				out_opCount += ops[tid];
			}
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_spmc: {
		counter_t elementsToDequeue = maxOps * (nthreads - 1);
		
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		std::vector<SimpleThread> threads(nthreads - 1);
		std::vector<double> timings(nthreads - 1);
		std::vector<counter_t> ops(nthreads - 1);
		std::atomic<bool> lynchpin(false);
		std::atomic<counter_t> totalDequeued(0);
		for (int tid = 0; tid != nthreads - 1; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				while (!lynchpin.load(std::memory_order_relaxed)) {
					continue;
				}
				
				int item;
				counter_t i = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					while (true) {
						if (q.try_dequeue(tok, item)) {
							totalDequeued.fetch_add(1, std::memory_order_relaxed);
						}
						else if (totalDequeued.load(std::memory_order_relaxed) == elementsToDequeue) {
							break;
						}
						++i;
					}
				}
				else {
					while (true) {
						if (q.try_dequeue(item_rec)) {
							totalDequeued.fetch_add(1, std::memory_order_relaxed);
						}
						else if (totalDequeued.load(std::memory_order_relaxed) == elementsToDequeue) {
							break;
						}
						++i;
					}
				}
				timings[id] = getTimeDelta(start);
				ops[id] = i;
			}, tid);
		}
		
		lynchpin.store(true, std::memory_order_seq_cst);
		for (counter_t i = 0; i != elementsToDequeue; ++i) {
			q.enqueue(item);
		}
		
		result = 0;
		out_opCount = 0;
		for (int tid = 0; tid != nthreads - 1; ++tid) {
			threads[tid].join();
			result += timings[tid];
			out_opCount += ops[tid];
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_mpsc: {
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		counter_t elementsToDequeue = maxOps * (nthreads - 1);
		std::vector<SimpleThread> threads(nthreads);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads; ++tid) {
			if (tid == 0) {
				// Consumer thread
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_seq_cst);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					out_opCount = 0;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != elementsToDequeue;) {
							i += q.try_dequeue(tok, item) ? 1 : 0;
							++out_opCount;
						}
					}
					else {
						for (counter_t i = 0; i != elementsToDequeue;) {
							i += q.try_dequeue(item_rec) ? 1 : 0;
							++out_opCount;
						}
					}
					result = getTimeDelta(start);
				}, tid);
			}
			else {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_seq_cst);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
		}
		
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	case bench_empty_dequeue: {
		// Measures the average speed of attempting to dequeue from an empty queue
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		// Fill up then empty the queue first
		{
			std::vector<SimpleThread> threads(maxThreads > 0 ? maxThreads : 8);
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](size_t id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != 10000; ++i) {
							q.enqueue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != 10000; ++i) {
							q.enqueue(item);
						}
					}
				}, tid);
			}
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
			}
			
			// Empty the queue
			while (q.try_dequeue(item_rec))
				continue;
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw failed dequeue speed on empty queue
			int item;
			out_opCount = maxOps;
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(tok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(item_rec);
				}
			}
			result = getTimeDelta(start);
			forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		}
		else {
			out_opCount = maxOps * nthreads;
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(item_rec);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
			forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		}
		break;
	}
	
	case bench_enqueue_dequeue_pairs: {
		// Measures the average speed of attempting to dequeue from an empty queue
		// (that eight separate threads had at one point enqueued to)
		out_opCount = maxOps * 2 * nthreads;
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		if (nthreads == 1) {
			// No contention -- measures speed of immediately dequeueing the item that was just enqueued
			int item;
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t prodTok(q);
				typename TQueue::consumer_token_t consTok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(prodTok, item);
					q.try_dequeue(consTok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(item);
					q.try_dequeue(item_rec);
				}
			}
			result = getTimeDelta(start);
			forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t prodTok(q);
						typename TQueue::consumer_token_t consTok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(prodTok, item);
							q.try_dequeue(consTok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(item);
							q.try_dequeue(item_rec);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result += timings[tid];
			}
			forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		}
		break;
	}
	
	case bench_heavy_concurrent: {
		// Measures the average operation speed with many threads under heavy load
		out_opCount = maxOps * nthreads;
		TQueue q;
		item_t item = 1;
		item_t item_rec;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (id < 2) {
					// Alternate
					int item;
					if (useTokens) {
						typename TQueue::consumer_token_t consTok(q);
						typename TQueue::producer_token_t prodTok(q);
						
						for (counter_t i = 0; i != maxOps / 2; ++i) {
							q.try_dequeue(consTok, item);
							q.enqueue(prodTok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps / 2; ++i) {
							q.try_dequeue(item_rec);
							q.enqueue(item);
						}
					}
				}
				else {
					if ((id & 1) == 0) {
						// Enqueue
						if (useTokens) {
							typename TQueue::producer_token_t prodTok(q);
							for (counter_t i = 0; i != maxOps; ++i) {
								q.enqueue(prodTok, item);
							}
						}
						else {
							for (counter_t i = 0; i != maxOps; ++i) {
								q.enqueue(item);
							}
						}
					}
					else {
						// Dequeue
						int item;
						if (useTokens) {
							typename TQueue::consumer_token_t consTok(q);
							for (counter_t i = 0; i != maxOps; ++i) {
								q.try_dequeue(consTok, item);
							}
						}
						else {
							for (counter_t i = 0; i != maxOps; ++i) {
								q.try_dequeue(item_rec);
							}
						}
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result += timings[tid];
		}
		forceNoOptimizeDummy = q.try_dequeue(item_rec) ? 1 : 0;
		break;
	}
	
	default:
		assert(false && "Every benchmark type must be handled here!");
		result = 0;
		out_opCount = 0;
	}
	
	(void)forceNoOptimizeDummy;
	
	return result;
}


const char* LOG_FILE = "benchmarks.log";
std::ofstream* logOut;
bool logErrorReported = false;

void sayf(int indent, const char* fmt, ...)
{
	static char indentBuffer[] = "                        ";
	static char buf[2048];
	
	indentBuffer[indent] = '\0';
	
	va_list arglist;
	va_start(arglist, fmt);
	vsprintf(buf, fmt, arglist);
	va_end(arglist);
	
	if (*logOut) {
		(*logOut) << indentBuffer << buf;
	}
	else if (!logErrorReported) {
		std::printf("Note: Error writing to log file. Future output will appear only on stdout\n");
		logErrorReported = true;
	}
	std::printf("%s%s", indentBuffer, buf);
	
	indentBuffer[indent] = ' ';
}


// Returns a formatted timestamp.
// Returned buffer is only valid until the next call.
// Not thread-safe.
static const char* timestamp()
{
	static char buf[32];
	time_t time = std::time(NULL);
	strcpy(buf, std::asctime(std::localtime(&time)));
	buf[strlen(buf) - 1] = '\0';	// Remove trailing newline
	return buf;
}

static inline bool isvowel(char ch)
{
	ch = std::tolower(ch);
	for (const char* v = "aeiou"; *v != '\0'; ++v) {
		if (*v == ch) {
			return true;
		}
	}
	return false;
}

static inline double safe_divide(double a, double b)
{
	return b == 0 ? 0 : a / b;
}

// Returns a positive number formatted in a string in a human-readable way.
// The string is always 7 characters or less (excluding null byte).
// Returned buffer is only valid until the sixteenth next call.
// Not thread safe.
static const char* pretty(double num)
{
	assert(num >= 0);
	
#if defined(_MSC_VER) && _MSC_VER < 1800
	if (!_finite(num)) {
		return "inf";
	}
	if (_isnan(num)) {
		return "nan";
	}
#else
	if (std::isinf(num)) {
		return "inf";
	}
	if (std::isnan(num)) {
		return "nan";
	}
#endif
	
	static char bufs[16][8];
	static int nextBuf = 0;
	char* buf = bufs[nextBuf++];
	nextBuf &= 15;
	
	int suffix = 0;
	if (num < 1) {
		static const char minisufs[] = "\0munpfazy";
		while (num < 0.01) {
			++suffix;
			num *= 1000;
		}
		sprintf(buf, "%1.4f%c", num, minisufs[suffix]);
	}
	else {
		static const char megasufs[] = "\0kMGTPEZY";
		while (num >= 1000) {
			++suffix;
			num /= 1000;
		}
		sprintf(buf, "%.2f%c", num, megasufs[suffix]);
	}
	
	return buf;
}

void printBenchmarkNames()
{
	std::printf("   Supported benchmarks are:\n");
	
	for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		std::printf("      %s\n", BENCHMARK_SHORT_NAMES[i]);
	}
}


int main(int argc, char** argv)
{
	// Disable buffering (so that when run in, e.g., Sublime Text, the output appears as it is written)
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	
	// Isolate the executable name
	std::string progName = argv[0];
	auto slash = progName.find_last_of("/\\");
	if (slash != std::string::npos) {
		progName = progName.substr(slash + 1);
	}
	
	std::map<std::string, benchmark_type_t> benchmarkMap;
	for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		benchmarkMap.insert(std::make_pair(std::string(BENCHMARK_SHORT_NAMES[i]), (benchmark_type_t)i));
	}
	std::vector<benchmark_type_t> selectedBenchmarks;
	
	bool showHelp = false;
	bool error = false;
	bool printedBenchmarks = false;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
			showHelp = true;
		}
		else if (std::strcmp(argv[i], "-p") == 0 || std::strcmp(argv[i], "--precise") == 0) {
			precise = true;
		}
		else if (std::strcmp(argv[i], "--run") == 0) {
			if (i + 1 == argc || argv[i + 1][0] == '-') {
				std::printf("Expected benchmark name argument for --run option.\n");
				if (!printedBenchmarks) {
					printBenchmarkNames();
					printedBenchmarks = true;
				}
				error = true;
				continue;
			}
			
			auto it = benchmarkMap.find(argv[++i]);
			if (it == benchmarkMap.end()) {
				std::printf("Unrecognized benchmark name '%s'.\n", argv[i]);
				if (!printedBenchmarks) {
					printBenchmarkNames();
					printedBenchmarks = true;
				}
				error = true;
				continue;
			}
			
			selectedBenchmarks.push_back(it->second);
		}
		else {
			std::printf("Unrecognized option '%s'\n", argv[i]);
			error = true;
		}
	}
	if (showHelp || error) {
		if (error) {
			std::printf("\n");
		}
		std::printf("%s\n    Description: Runs benchmarks for moodycamel::ConcurrentQueue\n", progName.c_str());
		std::printf("    --help            Prints this help blurb\n");
		std::printf("    --precise         Generate more precise benchmark results (slower)\n");
		std::printf("    --run benchmark   Runs only the selected benchmark (can be used multiple times)\n");
		return error ? 1 : 0;
	}
	
	bool logExists = true;
	{
		std::ifstream fin(LOG_FILE);
		if (!fin) {
			logExists = false;
		}
	}
	
	std::ofstream fout(LOG_FILE, std::ios::app);
	logOut = &fout;
	if (fout) {
		if (logExists) {
			fout << "\n\n\n";
		}
		fout << "--- New run (" << timestamp() << ") ---\n";
	}
	else {
		std::printf("Note: Error opening log file '%s'. Output will appear only on stdout.\n\n", LOG_FILE);
		logErrorReported = true;
	}
	
	const char* bitStr = "";
	if (sizeof(void*) == 4 || sizeof(void*) == 8) {
		bitStr = sizeof(void*) == 4 ? " 32-bit" : " 64-bit";
	}
	
	const char* cpuStr = getCPUString();
	sayf(0, "Running%s benchmarks on a%s %s\n", bitStr, isvowel(cpuStr[0]) ? "n" : "", cpuStr);
	if (precise) {
		sayf(4, "(precise mode)\n");
	}
	if (selectedBenchmarks.size() > 0) {
		sayf(4, "(selected benchmarks only)\n");
	}
	sayf(0, "Note that these are synthetic benchmarks. Take them with a grain of salt.\n\n");
	
	sayf(0, "Legend:\n");
	sayf(4, "'Avg':     Average time taken per operation, normalized to be per thread\n");
	sayf(4, "'Range':   The minimum and maximum times taken per operation (per thread)\n");
	sayf(4, "'Ops/s':   Overall operations per second\n");
	sayf(4, "'Ops/s/t': Operations per second per thread (inverse of 'Avg')\n");
	sayf(4, "Operations include those that fail (e.g. because the queue is empty).\n");
	sayf(4, "Each logical enqueue/dequeue counts as an individual operation when in bulk.\n");
	sayf(0, "\n");
	
	
#ifdef NDEBUG
	const int ITERATIONS = precise ? 100 : 10;
#else
	const int ITERATIONS = precise ? 20 : 2;
#endif
	
	
	const double FASTEST_PERCENT_CONSIDERED = precise ? 8 : 50;	// Only consider the top % of runs
	
	// Make sure each run of a given benchmark has the same seed (otherwise different runs are not comparable)
	std::srand(std::time(NULL));
	unsigned int randSeeds[BENCHMARK_TYPE_COUNT];
	for (unsigned int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		randSeeds[i] = std::rand() * (i + 1) + 1;
	}
	
	double opsst = 0;		// ops/s/thread
	
	double totalWeightedOpsst[QUEUE_COUNT];
	double totalWeight[QUEUE_COUNT];
	for (int i = 0; i != QUEUE_COUNT; ++i) {
		totalWeightedOpsst[i] = 0;
		totalWeight[i] = 0;
	}
	
	auto logicalCores = std::thread::hardware_concurrency();
	
	if (selectedBenchmarks.size() == 0) {
		for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
			selectedBenchmarks.push_back((benchmark_type_t)i);
		}
	}
	
	int indent = 0;
	for (auto selectedIt = selectedBenchmarks.cbegin(); selectedIt != selectedBenchmarks.cend(); ++selectedIt) {
		int benchmark = static_cast<int>(*selectedIt);
		auto seed = randSeeds[benchmark];
		
		bool anyQueueSupportsBenchmark = false;
		for (int queue = 0; queue != QUEUE_COUNT; ++queue) {
			if (QUEUE_BENCH_SUPPORT[queue][benchmark]) {
				anyQueueSupportsBenchmark = true;
				break;
			}
		}
		if (!anyQueueSupportsBenchmark) {
			continue;
		}
		
		sayf(0, "%s", BENCHMARK_NAMES[benchmark]);
		if (BENCHMARK_THREADS_MEASURED[benchmark] != 0) {
			if (BENCHMARK_THREADS_MEASURED[benchmark] < 0) {
				sayf(0, " (measuring all but %d %s)", -BENCHMARK_THREADS_MEASURED[benchmark], BENCHMARK_THREADS_MEASURED[benchmark] == -1 ? "thread" : "threads");
			}
			else {
				sayf(0, " (measuring %d %s)", BENCHMARK_THREADS_MEASURED[benchmark], BENCHMARK_THREADS_MEASURED[benchmark] == 1 ? "thread" : "threads");
			}
		}
		sayf(0, ":\n");
		indent += 2;
		sayf(indent, "(%s)\n", BENCHMARK_DESCS[benchmark]);
		
		for (int queue = 0; queue != QUEUE_COUNT; ++queue) {
			sayf(indent, "> %s\n", QUEUE_NAMES[queue]);
			
			if (!QUEUE_BENCH_SUPPORT[queue][benchmark]) {
				sayf(indent + 3, "(skipping, benchmark not supported...)\n\n");
				continue;
			}
			
			if (QUEUE_TOKEN_SUPPORT[queue]) {
				indent += 4;
			}
			for (int useTokens = 0; useTokens != 2; ++useTokens) {
				if (QUEUE_TOKEN_SUPPORT[queue]) {
					sayf(indent, "%s tokens\n", useTokens == 0 ? "Without" : "With");
				}
				if (useTokens == 1 && !QUEUE_TOKEN_SUPPORT[queue]) {
					continue;
				}
				indent += 3;
				
				std::vector<double> opssts;
				std::vector<int> threadCounts;
				for (int nthreadIndex = 0; BENCHMARK_THREADS[benchmark][nthreadIndex] != 0; ++nthreadIndex) {
					int nthreads = BENCHMARK_THREADS[benchmark][nthreadIndex];
					int measuredThreads = nthreads;
					if (BENCHMARK_THREADS_MEASURED[benchmark] != 0) {
						measuredThreads = BENCHMARK_THREADS_MEASURED[benchmark] < 0 ? nthreads + BENCHMARK_THREADS_MEASURED[benchmark] : BENCHMARK_THREADS_MEASURED[benchmark];
					}
					
					if (logicalCores > 0 && (unsigned int)nthreads > 3 * logicalCores) {
						continue;
					}
					if (QUEUE_MAX_THREADS[queue] >= 0 && QUEUE_MAX_THREADS[queue] < nthreads) {
						continue;
					}
					
					counter_t maxOps;
					switch ((queue_id_t)queue) {
					case queue_moodycamel_ConcurrentQueue:
						maxOps = determineMaxOpsForBenchmark<moodycamel::ConcurrentQueue<int, Traits>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_moodycamel_BlockingConcurrentQueue:
						maxOps = determineMaxOpsForBenchmark<moodycamel::BlockingConcurrentQueue<int, Traits>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_lockbased:
						maxOps = determineMaxOpsForBenchmark<LockBasedQueue<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_simplelockfree:
						maxOps = determineMaxOpsForBenchmark<SimpleLockFreeQueue<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_boost:
						maxOps = determineMaxOpsForBenchmark<BoostQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_tbb:
						maxOps = determineMaxOpsForBenchmark<TbbQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_std:
						maxOps = determineMaxOpsForBenchmark<StdQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					case queue_dlib:
						maxOps = determineMaxOpsForBenchmark<DlibQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed);
						break;
					default:
						assert(false && "There should be a case here for every queue in the benchmarks!");
					}
					//std::printf("maxOps: %llu\n", maxOps);
					
					int maxThreads = QUEUE_MAX_THREADS[queue];
					std::vector<BenchmarkResult> results(ITERATIONS);
					for (int i = 0; i < ITERATIONS; ++i) {
						double elapsed;
						counter_t ops = 0;
						
						switch ((queue_id_t)queue) {
						case queue_moodycamel_ConcurrentQueue:
							elapsed = runBenchmark<moodycamel::ConcurrentQueue<int, Traits>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_moodycamel_BlockingConcurrentQueue:
							elapsed = runBenchmark<moodycamel::BlockingConcurrentQueue<int, Traits>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_lockbased:
							elapsed = runBenchmark<LockBasedQueue<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_simplelockfree:
							elapsed = runBenchmark<SimpleLockFreeQueue<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_boost:
							elapsed = runBenchmark<BoostQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_tbb:
							elapsed = runBenchmark<TbbQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_std:
							elapsed = runBenchmark<StdQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						case queue_dlib:
							elapsed = runBenchmark<DlibQueueWrapper<int>, int>((benchmark_type_t)benchmark, nthreads, (bool)useTokens, seed, maxOps, maxThreads, ops);
							break;
						default:
							assert(false && "There should be a case here for every queue in the benchmarks!");
						}

						results[i].elapsedTime = elapsed;
						results[i].operations = ops;
					}
					
					std::sort(&results[0], &results[0] + ITERATIONS);
					int consideredCount = std::max(2, (int)(ITERATIONS * FASTEST_PERCENT_CONSIDERED / 100));
					
					double min = safe_divide(results[0].elapsedTime / 1000.0, (double)results[0].operations / measuredThreads);
					double max = safe_divide(results[0].elapsedTime / 1000.0, (double)results[0].operations / measuredThreads);
					double ops = 0;
					double time = 0;
					for (int i = 0; i != consideredCount; ++i) {
						double msPerOperation = safe_divide(results[i].elapsedTime / 1000.0, (double)results[i].operations / measuredThreads);
						if (msPerOperation < min) {
							min = msPerOperation;
						}
						else if (msPerOperation > max) {
							max = msPerOperation;
						}
						
						time += results[i].elapsedTime;
						ops += results[i].operations;
					}
					
					double avg = safe_divide(time / 1000.0, ops / measuredThreads);
					double opsPerSecond = safe_divide(ops, time / 1000.0);
					opsst = opsPerSecond / (double)measuredThreads;
					
					opssts.push_back(opsst);
					threadCounts.push_back(measuredThreads);
					
					sayf(indent, "%-3d %7s:  Avg: %7ss  Range: [%7ss, %7ss]  Ops/s: %7s  Ops/s/t: %7s\n", nthreads, nthreads != 1 ? "threads" : "thread", pretty(avg), pretty(min), pretty(max), pretty(opsPerSecond), pretty(opsst));
					if (nthreads == 1 && BENCHMARK_SINGLE_THREAD_NOTES[benchmark][0] != '\0') {
						sayf(indent + 7, "^ Note: %s\n", BENCHMARK_SINGLE_THREAD_NOTES[benchmark]);
					}
				}
				
				opsst = 0;
				double divisor = 0;
				for (size_t i = 0; i != opssts.size(); ++i) {
					opsst += opssts[i] * std::sqrt(threadCounts[i]);
					totalWeightedOpsst[queue] += opssts[i] * std::sqrt(threadCounts[i]);
					divisor += std::sqrt(threadCounts[i]);
					totalWeight[queue] += std::sqrt(threadCounts[i]);
				}
				opsst /= divisor;
				sayf(indent, "Operations per second per thread (weighted average): %7s\n\n", opsst == 0 ? "(n/a)" : pretty(opsst));
				
				indent -= 3;
			}
			if (QUEUE_TOKEN_SUPPORT[queue]) {
				indent -= 4;
			}
		}
		indent -= 2;
	}
	
	sayf(0, "Overall average operations per second per thread (where higher-concurrency runs have more weight):\n");
	sayf(0, "(Take this summary with a grain of salt -- look at the individual benchmark results for a much\nbetter idea of how the queues measure up to each other):\n");
	for (int queue = 0; queue != QUEUE_COUNT; ++queue) {
		opsst = safe_divide(totalWeightedOpsst[queue], totalWeight[queue]);
		if (QUEUE_SUMMARY_NOTES[queue] != nullptr && QUEUE_SUMMARY_NOTES[queue][0] != '\0') {
			sayf(4, "%s (%s): %7s\n", QUEUE_NAMES[queue], QUEUE_SUMMARY_NOTES[queue], opsst == 0 ? "(n/a)" : pretty(opsst));
		}
		else {
			sayf(4, "%s: %7s\n", QUEUE_NAMES[queue], opsst == 0 ? "(n/a)" : pretty(opsst));
		}
	}
	
	return 0;
}
