#pragma once

#include "../../relacy/relacy_std.hpp"


intptr_t const lock_value		        = (intptr_t)-1;

struct rdesc
{
    rl::var<std::atomic<intptr_t> const*> addr;
    rl::var<intptr_t>           cmp;
};

struct wdesc
{
    rl::var<std::atomic<intptr_t>*> addr;
    rl::var<intptr_t>           cmp;
    rl::var<intptr_t>           xchg;
};

struct trx
{
	static size_t const rset_max_size   = 64;
	static size_t const wset_max_size   = 32;

	rl::var<size_t>             rset_idx;
	rl::var<size_t>			    wset_idx;
    rdesc			rset	            [rset_max_size];
    wdesc			wset                [wset_max_size];

    rl::var<rdesc*> read(std::atomic<intptr_t> const* addr, std::memory_order mo = std::memory_order_relaxed)
    {
        intptr_t value = (*addr)($).load(mo);
		if (lock_value == value)
			return 0;
        rdesc* desc = &rset[rset_idx($)];
		++rset_idx($);
        desc->addr($) = addr;
        desc->cmp($) = value;
		return desc;
    }

    rl::var<wdesc*> write(std::atomic<intptr_t>* addr)
    {
        intptr_t value = (*addr)($).swap(lock_value, rl::memory_order_acq_rel);
		if (lock_value == value)
			return 0;
        wdesc* desc = &wset[wset_idx($)];
		++wset_idx($);
		desc->addr($) = addr;
		desc->cmp($) = value;
		return desc;
    }

    bool begin()
    {
        std::atomic_signal_fence($)(std::memory_order_acquire);
		rset_idx($) = 0;
		wset_idx($) = 0;
		return true;
    }

    bool commit()
    {
        std::atomic_signal_fence($)(std::memory_order_release);

        size_t i;
        for (i = 0; i != rset_idx($); ++i)
        {
            rdesc const* desc = &rset[i];
            if ((*(desc->addr($)))($).load(std::memory_order_relaxed) != desc->cmp($))
                break;
        }
        if (i != rset_idx($))
        {
            return rollback();
        }

        std::atomic_thread_fence($)(std::memory_order_release);

        for (i = 0; i != wset_idx($); ++i)
        {
            wdesc const* desc = &wset[i];
            (*(desc->addr($)))($).store(desc->xchg($), std::memory_order_relaxed);
        }

        //std::atomic_thread_fence(std::memory_order_acq_rel);

        return true;
    }

    bool rollback()
    {
        for (size_t i = 0; i != wset_idx($); ++i)
        {
            wdesc const* desc = &wset[i];
            (*(desc->addr($)))($).store(desc->cmp($), std::memory_order_relaxed);
        }
		wset_idx($) = 0;
		rset_idx($) = 0;
		return false;
    }

    /*
	bool readset_validate()
    {
        for (size_t i = 0; i != rset_idx; ++i)
        {
            rdesc const* desc = &rset[i];
            if (*(intptr_t const volatile*)desc->addr != desc->cmp)
                return true;
        }
        return false;
    }

    bool writeset_load(intptr_t* addr, intptr_t* value)
    {
        for (size_t i = 0; i != wset_idx; ++i)
        {
            wdesc const* desc = &wset[i];
            if (desc->addr == addr)
            {
                *value = desc->xchg;
                return true;
            }
        }
        return false;
    }
    */
};




inline void pdr_lock()
{
}

inline void pdr_unlock()
{
}

inline void pdr_acquire(void*)
{
}

inline void pdr_release(void*)
{
}

inline void pdr_dispose(void*)
{
}




struct dlist_trx_node
{
    std::atomic<intptr_t> prev; // dlist_trx_node*
    std::atomic<intptr_t> next; // dlist_trx_node*
    rl::var<intptr_t> key;
    rl::var<intptr_t> value;

    dlist_trx_node(intptr_t key = 0, intptr_t value = 0)
        : key(key)
        , value(value)
    {}
};





class dlist_trx
{
public:
    dlist_trx()
        : first(0, 0)
        , last(0, 0)
    {
        first.prev($).store(0, std::memory_order_relaxed);
        first.next($).store((intptr_t)&last, std::memory_order_relaxed);
        last.prev($).store((intptr_t)&first, std::memory_order_relaxed);
        last.next($).store(0, std::memory_order_relaxed);
    }

    __declspec(noinline) void remove(dlist_trx_node* node)
    {
        pdr_lock();

        for (trx t; t.begin(); t.rollback())
        {
			rdesc* r1 = t.read(&node->prev)($);
			if (0 == r1)
				continue;
			dlist_trx_node* prev = (dlist_trx_node*)(intptr_t)r1->cmp($);

			rdesc* r2 = t.read(&node->next)($);
			if (0 == r2)
				continue;
			dlist_trx_node* next = (dlist_trx_node*)(intptr_t)r2->cmp($);

			wdesc* w1 = t.write(&prev->next)($);
			if (0 == w1)
				continue;
			//dlist_trx_node* prev_next = (dlist_trx_node*)w1->cmp;

			wdesc* w2 = t.write(&next->prev)($);
			if (0 == w2)
				continue;
			//dlist_trx_node* next_prev = (dlist_trx_node*)w2->cmp;

			w1->xchg($) = (intptr_t)next;
            w2->xchg($) = (intptr_t)prev;

            if (t.commit())
                break;
        }

        pdr_unlock();
    }

    __declspec(noinline) void insert(dlist_trx_node* node)
    {
        pdr_lock();

		for (trx t; t.begin(); t.rollback())
        {
			wdesc* w1 = t.write(&first.next)($);
            if (0 == w1)
				continue;
			dlist_trx_node* next = (dlist_trx_node*)(intptr_t)w1->cmp($);

			wdesc* w2 = t.write(&next->prev)($);
			if (0 == w2)
                continue;
			dlist_trx_node* const& prev = (dlist_trx_node*)(intptr_t)w2->cmp($);

            if (prev != &first)
                continue;

            node->prev($).store((intptr_t)prev, std::memory_order_relaxed);
            node->next($).store((intptr_t)next, std::memory_order_relaxed);
			w1->xchg($) = (intptr_t)node;
			w2->xchg($) = (intptr_t)node;

            if (t.commit())
                break;
        }

        pdr_unlock();
    }

    __declspec(noinline) void foreach(void (*f)(void*, dlist_trx_node*), void (*reset)(void*), void* ctx)
    {
        pdr_lock();

        for (trx t; t.begin(); t.rollback())
        {
            reset(ctx);

            rdesc* r1 = t.read(&first.next, std::memory_order_consume)($);
            if (0 == r1)
                continue;
            dlist_trx_node* node = (dlist_trx_node*)(intptr_t)r1->cmp($);

            while (node->next($).load(std::memory_order_consume))
            {
                rdesc* r = t.read(&node->next)($);
				if (0 == r)
                    break;
				dlist_trx_node* next = (dlist_trx_node*)(intptr_t)r->cmp($);

                f(ctx, node);
                node = next;
            }
            if (node->next($).load(std::memory_order_relaxed))
                continue;

            if (t.commit())
                break;
        }

        pdr_unlock();
    }

    dlist_trx_node first;
    dlist_trx_node last;
};




struct dlist_trx_test : rl::test_suite<dlist_trx_test, 4>
{
    dlist_trx list;

    static int const count = 4;
    dlist_trx_node nodes[2][count];

    void thread(unsigned index)
    {
        if (0 == index || 1 == index)
        {
            for (int i = 0; i != count; ++i)
            {
                dlist_trx_node* n = &nodes[index][i];
                intptr_t value = 1 << ((index * count + i) * 4);
                n->key($) = value;
                n->value($) = value;
                list.insert(n);
            }
            for (int i = 0; i != count; ++i)
            {
                dlist_trx_node* n = &nodes[index][i];
                list.remove(n);
            }
        }
        else if (2 == index || 3 == index)
        {
            struct local
            {
                static void reset(void* ctx)
                {
                    *(int*)ctx = 0;
                }

                static void apply(void* ctx, dlist_trx_node* n)
                {
                    *(int*)ctx += (int)n->value($);
                }
            };

            int volatile sum = 0;
            list.foreach(&local::apply, &local::reset, (void*)&sum);
            int volatile x = sum;
            (void)x;
        }
    }

    void invariant()
    {
        int volatile sum = 0;
        dlist_trx_node* n = (dlist_trx_node*)list.first.next($).load();
        for (;;)
        {
            if (lock_value == (intptr_t)n)
                break;
            dlist_trx_node* next = (dlist_trx_node*)n->next($).load();
            if (0 == next)
                break;
            sum += (int)n->value($);
            n = next;
        }
    }
};


