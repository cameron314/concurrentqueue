// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

// Fuzz (random) tests for moodycamel::ConcurrentQueue

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cassert>
#include <string>
#include <random>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <vector>
#include <csignal>
#include <mutex>
#include <exception>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../../concurrentqueue.h"
#include "../common/simplethread.h"
#include "../common/systemtime.h"
#include "../corealgos.h"

void failHook()
{
	(void)1;		// Attach debuggers here
}

#define _STR(x) #x
#define STR(x) _STR(x)
#define ASSERT_OR_FAIL_THREAD(cond) if (!(cond)) { const char* n = nullptr; failReason.compare_exchange_strong(n, "assertion failed on line " STR(__LINE__) ": " #cond, std::memory_order_relaxed, std::memory_order_relaxed); \
                                                   failed.store(true, std::memory_order_relaxed); failHook(); return; }
#define FAIL_IF_THREAD_TIMEOUT() if (getTimeDelta(startTime) > 60000) { const char* n = nullptr; failReason.compare_exchange_strong(n, "test timed out (detected on line " STR(__LINE__) ")", std::memory_order_relaxed, std::memory_order_relaxed); \
                                                                        failed.store(true, std::memory_order_relaxed); failHook(); return; }
#define ASSERT_OR_FAIL(cond) if (!(cond)) { out_failReason = "assertion failed on line " STR(__LINE__) ": " #cond; result = false; failHook(); break; }


using namespace moodycamel;


typedef std::minstd_rand RNG_t;

enum test_type {
	multithread_produce,
	multithread_consume,
	multithread_produce_and_consume,
	completely_random,
	
	// Core algo tests
	core_add_only_list,
	core_thread_local,
	
	TEST_TYPE_COUNT
};

std::uint64_t test_count[TEST_TYPE_COUNT] = { 0 };
std::uint64_t fail_count[TEST_TYPE_COUNT] = { 0 };
const char* test_names[TEST_TYPE_COUNT] = {
	"multithread_produce",
	"multithread_consume",
	"multithread_produce_and_consume",
	"completely_random",
	"core_add_only_list",
	"core_thread_local",
};

const int SINGLE_SEED_ITERATIONS = 100;
const char* LOG_FILE = "fuzztests.log";


struct FuzzTraits : public ConcurrentQueueDefaultTraits
{
	static const size_t BLOCK_SIZE = 8;
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = 4;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 4;
	static const size_t INITIAL_IMPLCICIT_PRODUCER_HASH_SIZE = 1;
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 24;
};

struct TestListItem : corealgos::ListItem
{
	int value;
	
	TestListItem() : value(0) { }
	explicit TestListItem(int value) : value(value) { }
	
	inline TestListItem* prev(std::memory_order order = std::memory_order_relaxed) const
	{
		return static_cast<TestListItem*>(concurrentListPrev.load(order));
	}
};


bool run_test(uint64_t seed, int iterations, test_type& out_type, const char*& out_failReason)
{
	bool result = true;
	RNG_t baseRng((unsigned int)seed);
	
	std::uniform_int_distribution<int> randTest(0, TEST_TYPE_COUNT - 1);
	std::uniform_int_distribution<int> randInitialSize(0, 70);
	
	auto type = static_cast<test_type>(randTest(baseRng));
	out_type = type;
	for (int iteration = 0; iteration != iterations; ++iteration) {
		RNG_t rng(baseRng);
		
		std::atomic<bool> failed(false);
		std::atomic<const char*> failReason;
		failReason = nullptr;
		SystemTime startTime = getSystemTime();
		
		switch (type) {
		case multithread_produce:
		{
			const int countIncrement = std::uniform_int_distribution<int>(1, 1000)(rng);
			int count = std::uniform_int_distribution<int>(0, 500)(rng) * countIncrement;
			int prodCount = std::uniform_int_distribution<int>(0, 6)(rng);
			bool useConsumerToken = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
			
			ConcurrentQueue<int, FuzzTraits> q(randInitialSize(rng));
			
			std::vector<SimpleThread> producers(prodCount);
			std::vector<bool> useProducerToken(prodCount);
			for (int i = 0; i != prodCount; ++i) {
				useProducerToken[i] = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
				producers[i] = SimpleThread([&](int i) {
					ProducerToken t(q);
					for (int j = 0; j != count && !failed.load(std::memory_order_relaxed); j += countIncrement) {
						if (useProducerToken[i]) {
							for (int k = 0; k != countIncrement; ++k) {
								ASSERT_OR_FAIL_THREAD(q.enqueue(t, (i << 24) | (k + j)));
							}
						}
						else {
							for (int k = 0; k != countIncrement; ++k) {
								ASSERT_OR_FAIL_THREAD(q.enqueue((i << 24) | (k + j)));
							}
						}
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			
			SimpleThread consumer([&]() {
				int item;
				std::vector<int> lastItems(prodCount);
				ConsumerToken t(q);
				
				for (int i = 0; i != prodCount; ++i) {
					lastItems[i] = -1;
				}
				
				for (int i = 0; i != count * prodCount && !failed.load(std::memory_order_relaxed);) {
					if (useConsumerToken) {
						for (int j = 0; j != 10000; ++j) {
							if (q.try_dequeue(t, item)) {
								++i;
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) < count);
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) == lastItems[item >> 24] + 1);
								lastItems[item >> 24] = (item & 0xFFFFFF);
							}
						}
					}
					else {
						for (int j = 0; j != 10000; ++j) {
							if (q.try_dequeue(item)) {
								++i;
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) < count);
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) == lastItems[item >> 24] + 1);
								lastItems[item >> 24] = (item & 0xFFFFFF);
							}
						}
					}
					FAIL_IF_THREAD_TIMEOUT();
				}
			});
			
			for (int i = 0; i != prodCount; ++i) {
				producers[i].join();
			}
			consumer.join();
			
			if (failed.load(std::memory_order_relaxed)) {
				break;
			}
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
			break;
		}
		case multithread_consume:
		{
			const int countIncrement = std::uniform_int_distribution<int>(1, 1000)(rng);
			int count = std::uniform_int_distribution<int>(0, 500)(rng) * countIncrement;
			int consCount = std::uniform_int_distribution<int>(0, 6)(rng);
			bool useProducerToken = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
			std::atomic<bool> producerDone(false);
			
			ConcurrentQueue<int, FuzzTraits> q(randInitialSize(rng));
			
			std::vector<SimpleThread> consumers(consCount);
			std::vector<bool> useConsumerToken(consCount);
			for (int i = 0; i != consCount; ++i) {
				useConsumerToken[i] = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
				consumers[i] = SimpleThread([&](int i) {
					int item, lastItem = -1;
					ConsumerToken t(q);
					
					bool doneConsuming = false;
					while (!doneConsuming && !failed.load(std::memory_order_relaxed)) {
						auto producerDoneLocal = producerDone.load(std::memory_order_acquire);
						
						if (useConsumerToken[i]) {
							for (int j = 0; j != 10000; ++j) {
								if (q.try_dequeue(t, item)) {
									ASSERT_OR_FAIL_THREAD(item >= 0 && item < count * consCount && item > lastItem);
									lastItem = item;
								}
								else if (producerDoneLocal) {
									doneConsuming = true;
									break;
								}
							}
						}
						else {
							for (int j = 0; j != 10000; ++j) {
								if (q.try_dequeue(item)) {
									ASSERT_OR_FAIL_THREAD(item >= 0 && item < count * consCount && item > lastItem);
									lastItem = item;
								}
								else if (producerDoneLocal)  {
									doneConsuming = true;
									break;
								}
							}
						}
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			
			SimpleThread producer([&]() {
				ProducerToken t(q);
				for (int i = 0; i != count * consCount && !failed.load(std::memory_order_relaxed); i += countIncrement) {
					if (useProducerToken) {
						for (int j = 0; j != countIncrement; ++j) {
							ASSERT_OR_FAIL_THREAD(q.enqueue(t, i + j));
						}
					}
					else {
						for (int j = 0; j != countIncrement; ++j) {
							ASSERT_OR_FAIL_THREAD(q.enqueue(i + j));
						}
					}
					FAIL_IF_THREAD_TIMEOUT();
				}
				producerDone.store(true, std::memory_order_release);
			});
			
			producer.join();
			for (int i = 0; i != consCount; ++i) {
				consumers[i].join();
			}
			
			if (failed.load(std::memory_order_relaxed)) {
				break;
			}
			
			int item;
			ASSERT_OR_FAIL(consCount == 0 || !q.try_dequeue(item));
			
			break;
		}
		case multithread_produce_and_consume:
		{
			const int countIncrement = std::uniform_int_distribution<int>(1, 1000)(rng);
			int count = std::uniform_int_distribution<int>(0, 500)(rng) * countIncrement;
			int prodCount = std::uniform_int_distribution<int>(0, 6)(rng);
			int consCount = std::uniform_int_distribution<int>(0, 6)(rng);
			std::atomic<bool> producersDone(false);
			
			ConcurrentQueue<int, FuzzTraits> q(randInitialSize(rng));
			
			std::vector<SimpleThread> producers(prodCount);
			std::vector<bool> useProducerToken(prodCount);
			for (int i = 0; i != prodCount; ++i) {
				useProducerToken[i] = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
				producers[i] = SimpleThread([&](int i) {
					ProducerToken t(q);
					for (int j = 0; j != count && !failed.load(std::memory_order_relaxed); j += countIncrement) {
						if (useProducerToken[i]) {
							for (int k = 0; k != countIncrement; ++k) {
								ASSERT_OR_FAIL_THREAD(q.enqueue(t, (i << 24) | (k + j)));
							}
						}
						else {
							for (int k = 0; k != countIncrement; ++k) {
								ASSERT_OR_FAIL_THREAD(q.enqueue((i << 24) | (k + j)));
							}
						}
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			
			std::vector<SimpleThread> consumers(consCount);
			std::vector<bool> useConsumerToken(consCount);
			for (int i = 0; i != consCount; ++i) {
				useConsumerToken[i] = static_cast<bool>(std::uniform_int_distribution<int>(0, 1)(rng));
				consumers[i] = SimpleThread([&](int i) {
					int item;
					std::vector<int> lastItems(prodCount);
					ConsumerToken t(q);
					
					for (int j = 0; j != prodCount; ++j) {
						lastItems[j] = -1;
					}
					
					bool doneConsuming = false;
					while (!doneConsuming && !failed.load(std::memory_order_relaxed)) {
						auto producersDoneLocal = producersDone.load(std::memory_order_acquire);
						
						if (useConsumerToken[i]) {
							for (int j = 0; j != 10000; ++j) {
								if (q.try_dequeue(t, item)) {
									ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) < count);
									ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) > lastItems[item >> 24]);
									lastItems[item >> 24] = item & 0xFFFFFF;
								}
								else if (producersDoneLocal) {
									doneConsuming = true;
									break;
								}
							}
						}
						else {
							for (int j = 0; j != 10000; ++j) {
								if (q.try_dequeue(item)) {
									ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) < count);
									ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) > lastItems[item >> 24]);
									lastItems[item >> 24] = item & 0xFFFFFF;
								}
								else if (producersDoneLocal)  {
									doneConsuming = true;
									break;
								}
							}
						}
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			
			for (int i = 0; i != prodCount; ++i) {
				producers[i].join();
			}
			producersDone.store(true, std::memory_order_release);
			for (int i = 0; i != consCount; ++i) {
				consumers[i].join();
			}
			
			if (failed.load(std::memory_order_relaxed)) {
				break;
			}
			
			int item;
			ASSERT_OR_FAIL(consCount == 0 || !q.try_dequeue(item));
			
			break;
		}
		case completely_random:
		{
			int threadCount = std::uniform_int_distribution<int>(0, 32)(rng);
			
			ConcurrentQueue<int, FuzzTraits> q(randInitialSize(rng));
			
			std::vector<SimpleThread> threads(threadCount);
			std::vector<unsigned int> seeds(threadCount);
			std::vector<unsigned int> opCounts(threadCount);
			unsigned int largestOpCount = 0;
			for (int i = 0; i != threadCount; ++i) {
				opCounts[i] = std::uniform_int_distribution<unsigned int>(0, 500000)(rng);
				if (opCounts[i] > largestOpCount) {
					largestOpCount = opCounts[i];
				}
			}
			// Note: If you're wondering where all the memory goes, it's mostly here!
			std::vector<unsigned int> itemStates(largestOpCount * threadCount * 2);
			for (std::size_t j = 0; j != itemStates.size(); ++j) {
				itemStates[j] = 0;
			}
			for (int i = 0; i != threadCount; ++i) {
				seeds[i] = std::uniform_int_distribution<unsigned int>(0, 0xFFFFFFFF)(rng);
				threads[i] = SimpleThread([&](int i) {
					RNG_t rng((unsigned int)seeds[i]);
					ConsumerToken ct(q);
					ProducerToken pt(q);
					int item;
					int opCount = opCounts[i];
					std::vector<int> lastItems(threadCount * 2);		// * 2 because there's two producer queues per thread (one implicit, one explicit)
					for (int j = 0; j != threadCount * 2; ++j) {
						lastItems[j] = -1;
					}
					for (int j = 0; j < opCount && !failed.load(std::memory_order_relaxed); ++j) {
						int op = std::uniform_int_distribution<int>(0, 7)(rng);
						unsigned int* state;
						switch (op) {
						case 0:
							state = &itemStates[(i * 2) * largestOpCount + j];
							ASSERT_OR_FAIL_THREAD(*state == 0);
							*state = 1;
							ASSERT_OR_FAIL_THREAD(q.enqueue(pt, ((i * 2) << 24) | j));
							break;
						case 1:
							state = &itemStates[(i * 2 + 1) * largestOpCount + j];
							ASSERT_OR_FAIL_THREAD(*state == 0);
							*state = 1;
							ASSERT_OR_FAIL_THREAD(q.enqueue(((i * 2 + 1) << 24) | j));
							break;
						case 2:
							if (q.try_dequeue(ct, item)) {
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) >= 0 && (item & 0xFFFFFF) < (int)largestOpCount);
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) > lastItems[item >> 24]);
								lastItems[item >> 24] = item & 0xFFFFFF;
								
								state = &itemStates[(item >> 24) * largestOpCount + (item & 0xFFFFFF)];
								ASSERT_OR_FAIL_THREAD(*state == 1);
								*state = 2;
							}
							break;
						case 3:
							if (q.try_dequeue(item)) {
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) >= 0 && (item & 0xFFFFFF) < (int)largestOpCount);
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) > lastItems[item >> 24]);
								lastItems[item >> 24] = item & 0xFFFFFF;
								
								state = &itemStates[(item >> 24) * largestOpCount + (item & 0xFFFFFF)];
								ASSERT_OR_FAIL_THREAD(*state == 1);
								*state = 2;
							}
							break;
							
						case 4:
						case 5: {
							std::vector<int> bulkData(std::min(opCount - j, std::uniform_int_distribution<int>(0, 1024)(rng)));
							for (std::size_t k = 0; k != bulkData.size(); ++k) {
								state = &itemStates[(i * 2 + op - 4) * largestOpCount + j + k];
								ASSERT_OR_FAIL_THREAD(*state == 0);
								*state = 1;
								bulkData[k] = ((i * 2 + op - 4) << 24) | (j + (int)k);
							}
							if (op == 4) {
								ASSERT_OR_FAIL_THREAD(q.enqueue_bulk(pt, bulkData.begin(), bulkData.size()));
							}
							else {
								ASSERT_OR_FAIL_THREAD(q.enqueue_bulk(bulkData.begin(), bulkData.size()));
							}
							j += (int)bulkData.size() - 1;
							break;
						}
							
						case 6:
						case 7: {
							std::vector<int> bulkData(std::min(opCount - j, std::uniform_int_distribution<int>(0, 1024)(rng)));
							std::size_t count = 0;
							if (op == 6) {
								count = q.try_dequeue_bulk(ct, bulkData.begin(), bulkData.size());
							}
							else {
								count = q.try_dequeue_bulk(bulkData.begin(), bulkData.size());
							}
							for (std::size_t k = 0; k != count; ++k) {
								auto item = bulkData[k];
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) >= 0 && (item & 0xFFFFFF) < (int)largestOpCount);
								ASSERT_OR_FAIL_THREAD((item & 0xFFFFFF) > lastItems[item >> 24]);
								lastItems[item >> 24] = item & 0xFFFFFF;
								
								state = &itemStates[(item >> 24) * largestOpCount + (item & 0xFFFFFF)];
								ASSERT_OR_FAIL_THREAD(*state == 1);
								*state = 2;
							}
							if (count > 0) {
								j += (int)count - 1;
							}
							break;
						}
						
						default:
							assert(false);
						}
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			
			for (int i = 0; i != threadCount; ++i) {
				threads[i].join();
			}
#if MCDBGQ_TRACKMEM
			auto stats = q.getMemStats();		// Make available under debugger
			((void)stats);
#endif		
			
			int item;
			while (q.try_dequeue(item)) {
				unsigned int* state = &itemStates[(item >> 24) * largestOpCount + (item & 0xFFFFFF)];
				ASSERT_OR_FAIL(*state == 1);
				*state = 2;
			}
			for (std::size_t j = 0; j != itemStates.size(); ++j) {
				ASSERT_OR_FAIL(itemStates[j] == 0 || itemStates[j] == 2);
			}
			
			if (failed.load(std::memory_order_relaxed)) {
				break;
			}
			break;
		}
		case core_add_only_list:
		{
			int threadCount = std::uniform_int_distribution<int>(0, 48)(rng);
			std::vector<SimpleThread> threads(threadCount);
			std::vector<int> opCounts(threadCount);
			
			for (int i = 0; i != threadCount; ++i) {
				opCounts[i] = std::uniform_int_distribution<int>(0, 500000)(rng);
			}
			
			std::size_t expectedMemUsage = 0;
			for (int i = 0; i != threadCount; ++i) {
				expectedMemUsage += opCounts[i] * sizeof(TestListItem);
			}
			
			corealgos::ConcurrentAddOnlyList<TestListItem> list;
			for (int i = 0; i != threadCount; ++i) {
				threads[i] = SimpleThread([&](int tid) {
					auto temp = expectedMemUsage;
					((void)temp);
					
					int opCount = opCounts[tid];
					for (int j = 0; j != opCount; ++j) {
						list.add(new TestListItem((tid << 24) | j));
					}
				}, i);
			}
			for (int i = 0; i != threadCount; ++i) {
				threads[i].join();
			}
			std::vector<int> lastItems(threadCount);
			for (int i = 0; i != threadCount; ++i) {
				lastItems[i] = opCounts[i];
			}
			auto tail = list.tail();
			while (tail != nullptr) {
				auto tid = tail->value >> 24;
				ASSERT_OR_FAIL(lastItems[tid] - 1 == (tail->value & 0xFFFFFF));
				--lastItems[tid];
				auto next = tail->prev();
				delete tail;
				tail = next;
			}
			break;
		}
		case core_thread_local:
		{
			int threadCount = std::uniform_int_distribution<int>(32, 256)(rng);
			std::vector<SimpleThread> threads(threadCount);
			std::vector<int> opCounts(threadCount);
			std::vector<int*> localData(threadCount);
			
			for (int i = 0; i != threadCount; ++i) {
				opCounts[i] = std::uniform_int_distribution<int>(10000, 250000)(rng);
			}
			
			corealgos::ThreadLocal<TestListItem> tls(1);
			for (int i = 0; i != threadCount; ++i) {
				threads[i] = SimpleThread([&](int tid) {
					auto p = tls.get_or_create();
					ASSERT_OR_FAIL_THREAD(p->value == 0);
					p->value = tid;
					localData[tid] = &p->value;
					
					int opCount = opCounts[tid];
					for (int j = 0; j != opCount; ++j) {
						auto q = tls.get_or_create();
						ASSERT_OR_FAIL_THREAD(q == p);
						ASSERT_OR_FAIL_THREAD(q->value == tid);
						FAIL_IF_THREAD_TIMEOUT();
					}
				}, i);
			}
			for (int i = 0; i != threadCount; ++i) {
				threads[i].join();
			}
			for (int i = 0; i != threadCount; ++i) {
				ASSERT_OR_FAIL(localData[i] != nullptr);
				ASSERT_OR_FAIL(*localData[i] == i);
			}
			break;
		}
		default:
			assert(false);
		}
		
		++test_count[type];
		if (failed.load(std::memory_order_relaxed)) {
			out_failReason = failReason.load(std::memory_order_relaxed);
			result = false;
		}
		if (!result) {
			++fail_count[type];
			break;
		}
	}
	return result;
}


static const char* timestamp()
{
	static char buf[32];
	time_t time = std::time(NULL);
	strcpy(buf, std::asctime(std::localtime(&time)));
	buf[strlen(buf) - 1] = '\0';	// Remove trailing newline
	return buf;
}

extern "C" { typedef void (*signal_handler_t)(int); }
static std::atomic<std::uint64_t> g_seed(0);
static std::atomic_flag reported_signal_error = ATOMIC_FLAG_INIT;
static std::atomic<signal_handler_t> g_prev_sigsegv(nullptr);
static std::atomic<signal_handler_t> g_prev_sigabrt(nullptr);
static std::mutex g_signal_handler_mutex;

void on_signal(int signal)
{
	if (reported_signal_error.test_and_set()) {
		return;
	}
	
	std::unique_lock<std::mutex> lock(g_signal_handler_mutex);
	auto seed = g_seed.load(std::memory_order_acquire);
	
	// Technically undefined behaviour to use stdlib functions,
	// but oh well
	const char* error = signal == SIGABRT ?
		"Abort detected (assertion failed?)" :
		"Segmentation fault detected!";
	
	{
		std::ofstream fout(LOG_FILE, std::ios::app);
		fout << "*** " << error << "\n      Seed: " << std::hex << seed << std::endl;
	}
	std::printf("*** %s\n      Seed: %08x%08x\n", error, (uint32_t)(seed >> 32), (uint32_t)(seed));
	std::fflush(stdout);
}

extern "C" void signal_handler(int signal)
{
	on_signal(signal);
	if (signal_handler_t handler_fn = g_prev_sigsegv.load(std::memory_order_relaxed)) {
		handler_fn(signal);
	}
	else {
		std::exit(signal);
	}
}

#ifdef _WIN32
LONG CALLBACK se_handler(PEXCEPTION_POINTERS info)
{
	if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		on_signal(SIGSEGV);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv)
{
	bool singleSeed = false;
	uint64_t seed = 0;
	
	// Disable buffering (so that when run in, e.g., Sublime Text, the output appears as it is written)
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	
	// Isolate the executable name
	std::string progName = argv[0];
	auto slash = progName.find_last_of("/\\");
	if (slash != std::string::npos) {
		progName = progName.substr(slash + 1);
	}
	
	// Parse command line options
	if (argc > 1) {
		bool printHelp = false;
		bool error = false;
		for (int i = 1; i < argc; ++i) {
			if (std::strcmp(argv[i], "--help") == 0) {
				printHelp = true;
			}
			else if (std::strcmp(argv[i], "--seed") == 0) {
				if (i + 1 == argc || argv[i + 1][0] == '-') {
					std::printf("Expected seed number argument for --seed option.\n");
					error = true;
					continue;
				}
				
				++i;
				seed = 0;
				// hex
				for (int j = 0; argv[i][j] != '\0'; ++j) {
					char ch = static_cast<char>(std::tolower(argv[i][j]));
					if (j == 1 && seed == 0 && ch == 'x') {
						continue;	// Skip 0x, if any
					}
					else if (ch >= 'a' && ch <= 'f') {
						seed = (seed << 4) | (10 + ch - 'a');
					}
					else if (ch >= '0' && ch <= '9') {
						seed = (seed << 4) | (ch - '0');
					}
					else {
						std::printf("Expected hex seed argument, found '%s' instead\n", argv[i]);
						error = true;
					}
				}
				singleSeed = true;
			}
			else {
				std::printf("Unrecognized option '%s'.\n\n", argv[i]);
				error = true;
			}
		}
		
		if (error || printHelp) {
			std::printf("%s\n    Description: Runs fuzz tests (randomized stability tests) for moodycamel::ConcurrentQueue\n", progName.c_str());
			std::printf("    An infinite series of random tests are run, each with a different seed.\nIf a test fails, the seed for that test is reported.\n");
			std::printf("    --help        Prints this help blurb\n");
			std::printf("    --seed N      Runs one test with the given seed\n");
			return error ? -1 : 0;
		}
	}
	
	
	{
		bool logExists = true;
		{
			std::ifstream fin(LOG_FILE);
			if (!fin) {
				logExists = false;
			}
		}
		
		std::ofstream fout(LOG_FILE, std::ios::app);
		if (logExists) {
			fout << "\n\n";
		}
		if (singleSeed) {
			std::printf("Running %d iterations of single test with seed %08x%08x.\n\n", SINGLE_SEED_ITERATIONS, (uint32_t)(seed >> 32), (uint32_t)(seed));
			
			fout << "--- New run (" << timestamp() << "): Executing " << SINGLE_SEED_ITERATIONS << " iterations of a single test with seed " << std::hex << seed << " ---" << std::endl;
		}
		else {
			std::printf("Running random fuzz tests for moodycamel::ConcurrentQueue.\n");
			std::printf("Press CTRL+C to exit.\n");
			std::printf("(Run %s --help for options.)\n\n", progName.c_str());
			
			fout << "--- New run (" << timestamp() << "): Executing random fuzz tests ---" << std::endl;
		}		
	}
	
	int result = 0;
	test_type test;
	const char* failReason;
	if (singleSeed) {
		if (!run_test(seed, SINGLE_SEED_ITERATIONS, test, failReason)) {
			result = 1;
			std::ofstream fout(LOG_FILE, std::ios::app);
			fout << test_names[test] << " failed: " << failReason << std::endl;
			std::printf("    %s failed: %s\n", test_names[test], failReason);
		}
		else {
			std::ofstream fout(LOG_FILE, std::ios::app);
			fout << test_names[test] << " succeeded!" << std::endl;
			std::printf("    %s succeeded!\n", test_names[test]);
		}
	}
	else {
#ifdef _WIN32
		AddVectoredExceptionHandler(1 /* first? */, &se_handler);
#endif
		
		uint32_t iteration = 0;
		while (true) {
			seed = (static_cast<uint64_t>(std::time(NULL)) << 32) | iteration++;
			// MurmurHash3 64-bit finalizer
			seed ^= seed >> 33;
			seed *= 0xff51afd7ed558ccd;
			seed ^= seed >> 33;
			seed *= 0xc4ceb9fe1a85ec53;
			
			g_seed.store(seed, std::memory_order_release);
			std::signal(SIGSEGV, signal_handler);
			std::signal(SIGABRT, signal_handler);
			
			int result;
			try {
				result = run_test(seed, 2, test, failReason);
			}
			catch (std::exception const& e) {
				std::ofstream fout(LOG_FILE, std::ios::app);
				fout << "*** Exception thrown: " << e.what() << "\n      Seed: " << std::hex << seed << "\n      Test: " << test_names[test] << std::endl;
				std::printf("*** Exception thrown: %s\n      Seed: %08x%08x\n      Test: %s\n\n", e.what(), (uint32_t)(seed >> 32), (uint32_t)(seed), test_names[test]);
				std::exit(2);		// There shouldn't be any exceptions!
			}
			catch (...) {
				std::ofstream fout(LOG_FILE, std::ios::app);
				fout << "*** Unknown exception thrown!\n      Seed: " << std::hex << seed << "\n      Test: " << test_names[test] << std::endl;
				std::printf("*** Unknown exception thrown!\n      Seed: %08x%08x\n      Test: %s\n\n", (uint32_t)(seed >> 32), (uint32_t)(seed), test_names[test]);
				std::exit(2);
			}
			
			std::signal(SIGSEGV, SIG_DFL);
			std::signal(SIGABRT, SIG_DFL);
			
			if (!result) {
				result = 1;
				std::ofstream fout(LOG_FILE, std::ios::app);
				fout << "*** Failure detected!\n      Seed: " << std::hex << seed << "\n      Test: " << test_names[test] << "\n      Reason: " << failReason << std::endl;
				std::printf("*** Failure detected!\n      Seed: %08x%08x\n      Test: %s\n      Reason: %s\n", (uint32_t)(seed >> 32), (uint32_t)(seed), test_names[test], failReason);
			}
			
			if ((iteration & 31) == 0) {
				std::uint64_t total = 0;
				
				char breakdown[128 * TEST_TYPE_COUNT];
				char* ptr = breakdown;
				for (int i = 0; i != TEST_TYPE_COUNT; ++i) {
					std::sprintf(ptr, "    %s: %llu successful, %llu failed\n", test_names[i], (unsigned long long)(test_count[i] - fail_count[i]), (unsigned long long)fail_count[i]);
					ptr += std::strlen(ptr);
					total += test_count[i];
				}
				
				std::ofstream fout(LOG_FILE, std::ios::app);
				fout << "Executed " << total << " tests so far:\n" << breakdown;
				std::printf("Executed %llu tests so far:\n%s", (unsigned long long)total, breakdown);
			}
		}
	}
	
	return result;
}
