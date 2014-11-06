#include "stdafx.h"


struct pdr
{
    __declspec(thread) static pdr* instance;

    static size_t const defer_limit = 1024;

    typedef void(*dtor_f)(void*);
    struct entry_t
    {
        dtor_f dtor;
        void* ctx;
    };
    entry_t defer_list [defer_limit];
    size_t pos;
    size_t pos0;
    size_t thread_count;

    size_t th [4];

    void init(size_t count)
    {
        //assert(0 == instance);
        instance = this;

        thread_count = count;
        pos = 0;
        pos0 = 0;
        for (size_t i = 0; i != thread_count; ++i)
        {
            th[i] = defer_limit;
        }
    }

    void fini()
    {
        for (size_t i = 0; i != thread_count; ++i)
        {
            assert(th[i] == defer_limit);
        }

        for (size_t i = pos0; i != pos; ++i)
        {
            assert(defer_list[i].dtor);
            defer_list[i].dtor(defer_list[i].ctx);
        }
        assert(this == instance);
        instance = 0;
    }

    void lock()
    {
        std::atomic_thread_fence($)(std::memory_order_seq_cst);

        assert(th[rl::ctx().threadx_->index_] == defer_limit);
        th[rl::ctx().threadx_->index_] = pos;
    }

    void unlock()
    {
        assert(th[rl::ctx().threadx_->index_] != defer_limit);
        th[rl::ctx().threadx_->index_] = defer_limit;
        pump();
    }

    template<typename T>
    static void dtor_impl(void* p)
    {
        RL_DELETE(static_cast<T*>(p));
    }

    template<typename T>
    void defer(T* p)
    {
        std::atomic_thread_fence($)(std::memory_order_seq_cst);

        assert(pos < defer_limit);
        entry_t& e = defer_list[pos++];
        e.dtor = &pdr::dtor_impl<T>;
        e.ctx = p;
        pump();
    }

    void pump()
    {
        if (pos0 == pos)
            return;
        size_t min_pos = pos;
        for (size_t i = 0; i != thread_count; ++i)
        {
            if (th[i] < min_pos)
                min_pos = th[i];
        }
        for (size_t i = pos0; i != min_pos; ++i)
        {
            assert(defer_list[i].dtor);
            defer_list[i].dtor(defer_list[i].ctx);
        }
        pos0 = min_pos;
    }
};

pdr* pdr::instance = 0;

void pdr_lock()
{
    assert(pdr::instance);
    pdr::instance->lock();
}

void pdr_unlock()
{
    assert(pdr::instance);
    pdr::instance->unlock();
}

template<typename T>
void pdr_defer(T* p)
{
    assert(pdr::instance);
    pdr::instance->defer(p);
}



class ws_deque
{
public:

    ws_deque()
    {
		bottom_.block_($) = 0;
		bottom_.real_block_id_ = 0;
		bottom_.real_index_ = 0;
		bottom_.block_id_ = 0;
		bottom_.index_ = 0;
		bottom_.block_seq_ = 0;
		bottom_.check_order_ = 1;

        top::info t = {};
		top_.block_($) = 0;
        top_.info_($) = t;

		alloc_block();
		bottom_.block_id_ = bottom_.block_($)->header_.id_;
		top_.block_($) = bottom_.block_($);
		t.top_block_id_ = static_cast<unsigned short>(top_.block_($).load()->header_.id_);
		t.bottom_block_id_ = static_cast<unsigned short>(top_.block_($).load()->header_.id_);
        top_.info_($) = t;
    }

	~ws_deque()
	{
		for (block* p = top_.block_($), *next; p; p = next)
		{
			next = p->header_.next_($).load(std::memory_order_relaxed);
            RL_DELETE(p);
		}
	}

    void push(void* const& i)
    {
        pdr_lock();

        push_unbalanced(i);
        rebalance();

        pdr_unlock();
    }

    void push_unbalanced(void* i)
    {
        RL_ASSERT(bottom_.block_($)->header_.id_);

        bottom_.block_($)->data_[bottom_.real_index_]($).store(i, std::memory_order_release);

		if (block::item_count - 1 != bottom_.real_index_)
		{
			bottom_.real_index_ += 1;
		}
		else
		{
			alloc_block();
		}
    }

    void rebalance()
    {
		if (0 == --bottom_.check_order_)
		{
			check_bottom();
		}
    }

    void* pop()
    {
        pdr_lock();

        rebalance();
        void* p = pop_unbalanced();

        pdr_unlock();

        return p;
    }

    void* pop_unbalanced()
    {
		//!!! optimize

		//! fast-path for empty deque

		//! make comparasion faster

		if ((bottom_.block_id_ != bottom_.real_block_id_
            || bottom_.index_ != bottom_.real_index_)
			&& bottom_.real_index_)
		{
			bottom_.real_index_ -= 1;
            void* i = bottom_.block_($)->data_[bottom_.real_index_]($).load(std::memory_order_consume);
			return i;
		}
		else
		{
			return pop_unbalanced_slow();
		}
    }

    void* pop_unbalanced_slow()
	{
		if (0 == bottom_.real_index_)
		{
            if (bottom_.real_block_id_ > bottom_.block_id_)
            {
			    return pop_slow();
            }
            else
            {
			    return 0;
            }
		}
		else
		{
			void* i;
			pop_check_result const rv = pop_check(i);
			if (pop_check_cont != rv)
				return pop_check_succ == rv ? i : 0;
			return pop_unbalanced(); // recursion, must succeed
		}
	}

    void* steal()
    {
        pdr_lock();

retry:
		for (;;)
		{
            block* old_b = top_.block_($).load(std::memory_order_acquire);
		    block* b = old_b;
			top::info old = top_.info_($).load(std::memory_order_consume);

			if (old.top_index_ == old.bottom_index_
				&& old.top_block_id_ == old.bottom_block_id_)
			{
                pdr_unlock();
				return 0;
			}

			if (b->header_.id_ != old.top_block_id_)
			{
				do
				{
					b = b->header_.next_($).load(std::memory_order_relaxed);
                    //RL_ASSERT(b);
                    //!!! temp stub - is it right?
                    // it seems that we always return 0 after we hit this goto
                    if (0 == b)
                        goto retry;
				}
				//!!! AV
				// b == 0
				while (b->header_.id_ != old.top_block_id_);

                if (top_.block_($).compare_swap(old_b, b, std::memory_order_seq_cst))
				{
					block* cur_b = old_b;
					do
					{
						pdr_defer(cur_b);
						cur_b = cur_b->header_.next_($).load(std::memory_order_relaxed);
					}
					while (cur_b != b);
				}
			}

			block* next_block = 0;
			top::info mod = old;

            void* i = b->data_[mod.top_index_]($).load(std::memory_order_consume);

			if (block::item_count - 1 == mod.top_index_)
			{
				next_block = b->header_.next_($).load(std::memory_order_relaxed);
				mod.top_block_id_ += 1;
				mod.top_index_ = 0;
			}
			else
			{
				mod.top_index_ += 1;
			}

			if (top_.info_($).compare_swap(old, mod, std::memory_order_seq_cst))
			{
				if (next_block)
				{
					if (top_.block_($).compare_swap(b, next_block))
					{
						pdr_defer(b);
					}
				}

                pdr_unlock();
				return i;
			}
		}
    }

	unsigned size() const
	{
        top::info const top = top_.info_($).load(std::memory_order_relaxed);
		//unsigned volatile const top_block_id = top_.info_.part_.top_block_id_;
		//unsigned volatile const top_index = top_.info_.part_.top_index_;

		if (bottom_.real_block_id_ == top.top_block_id_)
		{
			unsigned const size = bottom_.real_index_ - top.top_index_;
			return size;
		}
		else
		{
			unsigned size = bottom_.real_index_;
			size += block::item_count - top.top_index_;
			size += (bottom_.real_block_id_ - top.top_block_id_ - 1) * block::item_count;
			return size;
		}
	}

private:
	struct block
	{
		struct header
		{
            std::atomic<block*> next_;
            std::atomic<block*> prev_;
			ws_deque* deque_;
			unsigned id_;

            //!!!
            ~header()
            {
                id_ = 0;
            }
		};
		static unsigned const item_count = 2;

		header header_;
        std::atomic<void*> data_ [item_count];
	};

	struct bottom
	{
        rl::var<block*> block_;
		
		unsigned check_order_;

		unsigned real_block_id_;
		unsigned real_index_;

		unsigned block_id_;
		unsigned index_;

		unsigned block_seq_;
	};

	struct top
	{
		struct info
		{
			unsigned short top_index_;
			unsigned short top_block_id_;
			unsigned short bottom_index_;
			unsigned short bottom_block_id_;

            bool operator == (info const& x) const
            {
                return top_index_ == x.top_index_
                    && top_block_id_ == x.top_block_id_
                    && bottom_index_ == x.bottom_index_
                    && bottom_block_id_ == x.bottom_block_id_;
            }

            friend std::ostream& operator << (std::ostream& ss, info const& x)
            {
                return ss << "{" << x.top_index_
                    << "," << x.top_block_id_
                    << "," << x.bottom_index_
                    << "," << x.bottom_block_id_ << "}";
            }
		};

        std::atomic<block*> block_;
        std::atomic<info> info_;
	};

	bottom bottom_;

	char pad1 [64];

	top top_;

	char pad2 [64];

	void alloc_block()
	{
		//!!! check whether we already have next block in 
		// bottom_.block_->header_.next_
		block* b = bottom_.block_($) ? bottom_.block_($)->header_.next_($).load(std::memory_order_relaxed) : 0;
		if (0 == b)
		{
			b = RL_NEW block;
			b->header_.deque_ = this;
			bottom_.block_seq_ += 1;

			//!!!
			if (bottom_.block_seq_ > 0xffff) __asm int 3;

			bottom_.block_seq_ &= 0xffff;
			b->header_.id_ = bottom_.block_seq_;
            b->header_.prev_($).store(bottom_.block_($), std::memory_order_relaxed);
			if (bottom_.block_($))
				bottom_.block_($)->header_.next_($).store(b, std::memory_order_relaxed);
			b->header_.next_($).store(0, std::memory_order_relaxed);
		}
		else
		{
            b = b;
			bottom_.block_seq_ += 1;
			//__asm int 3;
		}
		bottom_.block_($) = b;
		bottom_.real_block_id_ = b->header_.id_;
		bottom_.real_index_ = 0;
	}

	enum pop_check_result {pop_check_fail, pop_check_succ, pop_check_cont};

	pop_check_result pop_check(void*& i)
	{
		check_bottom();

		if (bottom_.block_id_ == bottom_.real_block_id_
			&& bottom_.index_ == bottom_.real_index_)
		{
            top::info const top = top_.info_($).load(std::memory_order_seq_cst);

			if ((bottom_.block_id_ == top.top_block_id_
					&& bottom_.index_ == (unsigned)top.top_index_ + 1)
				|| (bottom_.block_id_ == (unsigned)top.top_block_id_ + 1
					&& block::item_count - 1 == top.top_index_
					&& 0 == bottom_.index_ ))
			{
                __asm int 3;
                i = steal();
				if (i)
					return pop_check_succ;
			}

			return pop_check_fail;
		}

		return pop_check_cont;
	}

	void* pop_slow()
	{
		bottom_.block_seq_ -= 1;
		bottom_.block_seq_ &= 0xffff;
        bottom_.block_($) = bottom_.block_($)->header_.prev_($).load(std::memory_order_relaxed);

		//!!! AV: when core count set to 16
		// bottom_.block_ = 0
		// bottom.real_block_id = 1
		// bottom.block_id = 8

		//!!! AV in xscale too (thread count is 4)
		// the same variables values
		bottom_.real_block_id_ = bottom_.block_($)->header_.id_;
		bottom_.real_index_ = block::item_count - 1;

        top::info i = top_.info_($).load(std::memory_order_relaxed);

        RL_ASSERT(bottom_.block_($)->header_.id_ == bottom_.block_seq_);
        RL_ASSERT((bottom_.real_block_id_ == i.bottom_block_id_ && bottom_.real_index_ >= i.bottom_index_)
            || (bottom_.real_block_id_ > i.bottom_block_id_));

        void* v = bottom_.block_($)->data_[block::item_count - 1]($).load(std::memory_order_consume);
        return v;
	}

	void check_bottom()
	{
		//!!! must leave at least 1 element unreserved
		// because owner have to steal it

		for (;;)
		{
            top::info old = top_.info_($).load(std::memory_order_relaxed);

			unsigned const top_block_id = old.top_block_id_;
			unsigned const top_index = old.top_index_;

			if (bottom_.real_block_id_ == top_block_id
				&& bottom_.real_index_ == top_index)
			{
				bottom_.check_order_ = 2;
				return;
			}
			unsigned const s = size();
			unsigned const r = reserved();
			if (!(0 == r || (r > 1 && 4*r > 3*s)))
			{
				//bottom_.check_order_ = 2;
				//!!! bottom_.check_order_ = s / 8 + 2;
				bottom_.check_order_ = s / 2 + 2;
				return;
			}
			unsigned r2 = s*3/4 + 1;
			if (r2 >= s)
				r2 = s - 1;
			unsigned bottom_block_id;
			unsigned bottom_index;
			if (r2 + top_index < block::item_count)
			{
				bottom_block_id = top_block_id;
				bottom_index = top_index + r2;
			}
			else
			{
				unsigned const r3 = r2 + top_index;
				bottom_block_id = top_block_id + r3 / block::item_count;
				bottom_index = r3 % block::item_count;
			}
			top::info i;
			i.top_block_id_ = static_cast<unsigned short>(top_block_id);
			i.top_index_ = static_cast<unsigned short>(top_index);
			i.bottom_block_id_ = static_cast<unsigned short>(bottom_block_id);
			i.bottom_index_ = static_cast<unsigned short>(bottom_index);

			/*
			bottom volatile btm = bottom_;
			if (i.part_.top_block_id_ > i.part_.bottom_block_id_)
				__asm int 3;
			if (i.part_.top_block_id_ == i.part_.bottom_block_id_
				&& i.part_.top_index_ >= i.part_.bottom_index_)
				__asm int 3;
			if (i.part_.bottom_block_id_ > btm.real_block_id_)
				__asm int 3;
			if (i.part_.bottom_block_id_ == btm.real_block_id_
				&& i.part_.bottom_index_ > btm.real_index_)
				__asm int 3;
			*/

            if (top_.info_($).compare_swap(old, i, std::memory_order_seq_cst))
			{
				bottom_.block_id_ = bottom_block_id;
				bottom_.index_ = bottom_index;
				//!!! bottom_.check_order_ = s / 8 + 2;
				bottom_.check_order_ = s / 2 + 2;

				return;
			}
		}
	}

	unsigned reserved() const
	{
		if (bottom_.real_block_id_ == bottom_.block_id_)
		{
			unsigned const reserved = bottom_.real_index_ - bottom_.index_;
			return reserved;
		}
		else
		{
			unsigned reserved = bottom_.real_index_;
			reserved += block::item_count - bottom_.index_;
			reserved += (bottom_.real_block_id_ - bottom_.block_id_ - 1) * block::item_count;
			return reserved;
		}
	}
};

int x = 0;

struct ws_deque_test : rl::test_suite<ws_deque_test, 4>
{
    ws_deque q;
    pdr p;

    void before()
    {
        p.init(4);
    }

    void after()
    {
        p.fini();
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (size_t i = 0; i != 4; ++i)
            {
                q.push((void*)10);
            }

            for (size_t i = 0; i != 5; ++i)
            {
                void* p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push((void*)10);
                void* p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push((void*)10);
                q.push((void*)10);
                void* p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
                p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.push((void*)10);
                q.push((void*)10);
                q.push((void*)10);
                void* p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
            }

            for (size_t i = 0; i != 14; ++i)
            {
                q.push((void*)10);
                void* p = q.pop();
                RL_ASSERT((void*)10 == p || 0 == p);
            }
        }
        else
        {
            for (size_t i = 0; i != 4; ++i)
            {
                void* p = q.steal();
                RL_ASSERT((void*)10 == p || 0 == p);
            }
        }
    }
};




int main()
{
    rl::test_params p;
    p.iteration_count = 1000000;
    rl::simulate<ws_deque_test>(p);
}

