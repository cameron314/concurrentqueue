// ©2014 Cameron Desrochers

#include "relacy/relacy/relacy_std.hpp"

namespace details
{
    template<typename U>
    static inline char* align_for(char* ptr)
    {
        const std::size_t alignment = std::alignment_of<U>::value;
        return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
    }
}

template<typename TValue>
struct SPMCSequentialHashMap
{
    explicit SPMCSequentialHashMap(std::size_t initialSize)
        : nextCapacity(initialSize), index(nullptr)
    {
        new_index();
    }
    
    ~SPMCSequentialHashMap()
    {
        auto ptr = index.load(std::memory_order_relaxed);
        if (ptr != nullptr) {
            for (std::size_t i = 0; i != ptr->capacity; ++i) {
                ptr->index[i]->~IndexEntry();
            }
            do {
                auto prev = ptr->prev;
                ptr->~IndexHeader();
                free(ptr);
                ptr = prev;
            } while (ptr != nullptr);
        }
    }
    
    // Not thread safe. Only call from single producer thread.
    // Note: key must *not* be in hash already, and must be exactly
    // one larger than the previously inserted key value.
    void insert(std::uint64_t key, TValue* value)
    {
        IndexEntry* idxEntry;
        insert_index_entry(idxEntry, key);
        idxEntry->value.store(value, std::memory_order_release);
    }
    
    // Thread-safe, but if somebody can remove the key while find() is
    // in progress, then any returned value is not guaranteed to correspond
    // to that key. This also applies if the key was not already present but
    // once was. Elements can be found in any order.
    TValue* find(std::uint64_t key)
    {
        auto idxEntry = get_entry_for_key(key);
        if (idxEntry == nullptr)
            return nullptr;
        return idxEntry->value.load(std::memory_order_acquire);
    }
    
    // Thread-safe, but if somebody else can remove the same key while remove()
    // is in progress, then any removed value is not guaranteed to correspond
    // to that key This also applies if the key was not already present but
    // once was. Elements can be removed in an order.
    TValue* remove(std::uint64_t key)
    {
        auto idxEntry = get_entry_for_key(key);
        if (idxEntry == nullptr)
            return nullptr;
        TValue* val = nullptr;
        while (!idxEntry->value.compare_exchange_weak(val, nullptr, std::memory_order_acquire, std::memory_order_relaxed))
            continue;
        return val;
    }
    
private:
    struct IndexEntry
    {
        std::atomic<std::uint64_t> key;
        std::atomic<TValue*> value;
    };
    
    struct IndexHeader
    {
        std::size_t capacity;
        std::atomic<std::size_t> tail;
        IndexEntry* entries;
        IndexEntry** index;
        IndexHeader* prev;
    };
    
    inline void insert_index_entry(IndexEntry*& idxEntry, std::uint64_t key)
    {
        auto localIndex = index.load(std::memory_order_relaxed);        // We're the only writer thread, relaxed is OK
        auto newTail = (localIndex->tail.load(std::memory_order_relaxed) + 1) & (localIndex->capacity - 1);
        idxEntry = localIndex->index[newTail];
        if (idxEntry->key.load(std::memory_order_relaxed) == INVALID_KEY ||
            idxEntry->value.load(std::memory_order_relaxed) == nullptr) {
            
            idxEntry->key.store(key, std::memory_order_relaxed);
            localIndex->tail.store(newTail, std::memory_order_release);
            return;
        }
        
        // No room in the old index, try to allocate another one!
        new_index();
        localIndex = index.load(std::memory_order_relaxed);
        newTail = (localIndex->tail.load(std::memory_order_relaxed) + 1) & (localIndex->capacity - 1);
        idxEntry = localIndex->index[newTail];
        assert(idxEntry->key.load(std::memory_order_relaxed) == INVALID_KEY);
        idxEntry->key.store(key, std::memory_order_relaxed);
        localIndex->tail.store(newTail, std::memory_order_release);
    }
    
    inline IndexEntry* get_entry_for_key(std::uint64_t key) const
    {
        auto localIndex = index.load(std::memory_order_acquire);
        auto tail = localIndex->tail.load(std::memory_order_acquire);
        auto tailBase = localIndex->index[tail]->key.load(std::memory_order_relaxed);
        if (tailBase == INVALID_KEY) {
            return nullptr;
        }
        auto offset = static_cast<std::size_t>(key - tailBase);
        std::size_t idx = (tail + offset) & (localIndex->capacity - 1);
        auto entry = localIndex->index[idx];
        return entry->key.load(std::memory_order_relaxed) == key ? entry : nullptr;
    }
    
    bool new_index()
    {
        auto prev = index.load(std::memory_order_relaxed);
        std::size_t prevCapacity = prev == nullptr ? 0 : prev->capacity;
        auto entryCount = prev == nullptr ? nextCapacity : prevCapacity;
        auto raw = static_cast<char*>(malloc(
            sizeof(IndexHeader) +
            std::alignment_of<IndexEntry>::value - 1 + sizeof(IndexEntry) * entryCount +
            std::alignment_of<IndexEntry*>::value - 1 + sizeof(IndexEntry*) * nextCapacity));
        if (raw == nullptr) {
            return false;
        }
        
        auto header = new (raw) IndexHeader;
        auto entries = reinterpret_cast<IndexEntry*>(details::align_for<IndexEntry>(raw + sizeof(IndexHeader)));
        auto idx = reinterpret_cast<IndexEntry**>(details::align_for<IndexEntry*>(reinterpret_cast<char*>(entries) + sizeof(IndexEntry) * entryCount));
        if (prev != nullptr) {
            auto prevTail = prev->tail.load(std::memory_order_relaxed);
            auto prevPos = prevTail;
            std::size_t i = 0;
            do {
                prevPos = (prevPos + 1) & (prev->capacity - 1);
                idx[i++] = prev->index[prevPos];
            } while (prevPos != prevTail);
            assert(i == prevCapacity);
        }
        for (std::size_t i = 0; i != entryCount; ++i) {
            new (entries + i) IndexEntry;
            entries[i].key.store(INVALID_KEY, std::memory_order_relaxed);
            entries[i].value.store(nullptr, std::memory_order_relaxed);
            idx[prevCapacity + i] = entries + i;
        }
        header->prev = prev;
        header->entries = entries;
        header->index = idx;
        header->capacity = nextCapacity;
        header->tail.store((prevCapacity - 1) & (nextCapacity - 1), std::memory_order_relaxed);
        
        index.store(header, std::memory_order_release);
        
        nextCapacity <<= 1;
        
        return true;
    }
        
    private:
        std::size_t nextCapacity;
        std::atomic<IndexHeader*> index;
        
        static const std::uint64_t INVALID_KEY = ~(std::uint64_t)0;
};



template<int ThreadCount, int NUM_VALUES>
struct test : rl::test_suite<test<ThreadCount, NUM_VALUES>, ThreadCount>
{
	SPMCSequentialHashMap<int>* hash;
    int values[NUM_VALUES];
    std::atomic<int> useCounts[NUM_VALUES];
	std::atomic<bool> removed[NUM_VALUES];
	
	void before()
	{
        hash = new SPMCSequentialHashMap<int>(2);
        for (int i = 0; i != NUM_VALUES; ++i) {
            values[i] = i;
            useCounts[i].store(0, std::memory_order_relaxed);
            removed[i].store(false, std::memory_order_relaxed);
        }
	}
	
	void thread(unsigned int tid)
	{
        if (tid == 0) {
            // Producer
            for (int i = 0; i != NUM_VALUES; ++i) {
                hash->insert(i, &values[i]);
                useCounts[i].store(ThreadCount / 2, std::memory_order_release);
            }
        }
		else {
            // Consumer
            for (int i = 0; i != NUM_VALUES; ++i) {
                auto useCount = useCounts[i].fetch_add(-1, std::memory_order_acquire);
                auto val = hash->find(i);
                bool isRemoved = removed[i].load(std::memory_order_relaxed);
                auto current = useCounts[i].fetch_add(0, std::memory_order_release);
                if (useCount > 0 && (current > 0 || current == 0 && useCount == 1)) {
                    RL_ASSERT(val != nullptr && *val == i && !isRemoved);
                }
                if (useCount == 1) {
                    val = hash->remove(i);
                    RL_ASSERT(val != nullptr && *val == i && !removed[i].load(std::memory_order_relaxed));
                    removed[i].store(true, std::memory_order_release);
                }
            }
        }
	}
	
	void after()
	{
        delete hash;
	}
	
	void invariant()
	{
	}
};

int main()
{
	rl::test_params params;
	//params.search_type = rl::sched_full;
	//params.iteration_count = 100000000;
	params.search_type = rl::sched_random;
	params.iteration_count = 1000000;
    rl::simulate<test<2, 4>>(params);
    rl::simulate<test<3, 4>>(params);
    rl::simulate<test<4, 8>>(params);
	
	return 0;
}
