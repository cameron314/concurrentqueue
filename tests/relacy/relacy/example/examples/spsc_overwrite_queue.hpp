#pragma once


template<typename T>
class queue
{
public:
    queue(size_t count)
    {
        assert(count >= 6);
        sema = CreateSemaphore(0, 0, 1, 0);
        waiting.store(false, std::memory_order_relaxed);
        deq_node = 0;
        block = new node [count];
        block->next.store(0, std::memory_order_relaxed);
        full_tail = block;
        full_head.store(block, std::memory_order_relaxed);
        free_head = block + 1;
        free_tail.store(block + count - 1, std::memory_order_relaxed);
        free_tail.load(std::memory_order_relaxed)->next.store(0, std::memory_order_relaxed);
        for (size_t i = 1; i != count - 1; i += 1)
            block[i].next.store(block + i + 1, std::memory_order_relaxed);
    }

    ~queue()
    {
        CloseHandle(sema);
        delete [] block;
    }

    VAR_T(T)& enqueue_prepare()
    {
        return full_tail->data;
    }

    void enqueue_commit()
    {
        node* n = get_free_node();
        n->next.store(0, std::memory_order_release);
        full_tail->next.store(n, std::memory_order_seq_cst);
        bool signal = waiting.load(std::memory_order_seq_cst);
        full_tail = n;
        if (signal)
        {
            waiting.store(false, std::memory_order_relaxed);
            ReleaseSemaphore(sema, 1, 0);
        }
    }

    VAR_T(T)& dequeue_prepare()
    {
        deq_node = get_full_node();
        return deq_node->data;
    }

    void dequeue_commit()
    {
        deq_node->next.store(0, std::memory_order_release);
        node* prev = free_tail.exchange(deq_node, std::memory_order_acq_rel);
        prev->next.store(deq_node, std::memory_order_release);
    }

private:
    struct node
    {
        std::atomic<node*>  next;
        VAR_T(T)            data;
    };

    node*                   block;
    node*                   full_tail;
    node*                   free_head;
    node*                   deq_node;
    char                    pad [64];
    std::atomic<node*>      full_head;
    std::atomic<node*>      free_tail;
    std::atomic<bool>       waiting;
    HANDLE                  sema;

    node* get_free_node()
    {
        for (;;)
        {
            node* n = free_head;
            node* next = n->next.load(std::memory_order_acquire);
            if (next)
            {
                free_head = next;
                return n;
            }

            n = full_head.load(std::memory_order_acquire);
            next = n->next.load(std::memory_order_acquire);
            if (next)
            {
                if (full_head.compare_exchange_strong(n, next, std::memory_order_seq_cst))
                {
                    //node* n2 = free_head;
                    //node* next2 = n2->next.load(std::memory_order_acquire);
                    //if (next2)
                    //{
                    //    n->next.store(0, std::memory_order_release);
                    //    node* prev = free_tail.exchange(n, std::memory_order_acq_rel);
                    //    prev->next.store(n, std::memory_order_release);
                    //    free_head = next2;
                    //    return n2;
                    //}
                    //else
                    {
                        return n;
                    }
                }
            }
        }
    }

    node* get_full_node()
    {
        node* n = full_head.load(std::memory_order_acquire);
        for (;;)
        {
            node* next = n->next.load(std::memory_order_acquire);
            if (next == 0)
            {
                waiting.store(true, std::memory_order_seq_cst);
                n = full_head.load(std::memory_order_seq_cst);
                next = n->next.load(std::memory_order_acquire);
                if (next)
                {
                    waiting.store(false, std::memory_order_relaxed);
                }
                else
                {
                    WaitForSingleObject(sema, INFINITE);
                    n = full_head.load(std::memory_order_acquire);
                    continue;
                }
            }
            if (full_head.compare_exchange_strong(n, next, std::memory_order_acq_rel))
                return n;
        }
    }
};



unsigned RL_STDCALL consumer_thread(void* ctx)
{
    queue<int>* q = (queue<int>*)ctx;
    int prev_data = -1;
    for (;;)
    {
        VAR_T(int)& data0 = q->dequeue_prepare();
        int data = VAR(data0);
        assert(data > prev_data);
        prev_data = data;
        q->dequeue_commit();
        //printf("%d\n", prev_data);
        if (prev_data == 11)
            break;
        //Sleep(5);
    }
    return 0;
}

unsigned RL_STDCALL producer_thread(void* ctx)
{
    queue<int>* q = (queue<int>*)ctx;
    for (int i = 0; i != 12; i += 1)
    {
        VAR_T(int)& data = q->enqueue_prepare();
        VAR(data) = i;
        q->enqueue_commit();
        //Sleep(1);
    }
    return 0;
}

void spsc_overwrite_queue_test()
{
    queue<int> q (6);
    HANDLE th [2];
    th[0] = (HANDLE)_beginthreadex(0, 0, consumer_thread, &q, 0, 0);
    th[1] = (HANDLE)_beginthreadex(0, 0, producer_thread, &q, 0, 0);
    WaitForMultipleObjects(2, th, 1, INFINITE);

    for (int i = 100; i != 104; i += 1)
    {
        VAR_T(int)& data = q.enqueue_prepare();
        VAR(data) = i;
        q.enqueue_commit();
    }

    for (int i = 100; i != 104; i += 1)
    {
        VAR_T(int)& data0 = q.dequeue_prepare();
        int data = VAR(data0);
        assert(data == i);
        q.dequeue_commit();
    }
}

