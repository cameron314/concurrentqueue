//#ifdef _FORTIFY_SOURCE
//#undef _FORTIFY_SOURCE
//#endif
//#define _FORTIFY_SOURCE 0

#include "../../relacy/pthread.h"

class queue_t
{
public:
    queue_t()
    {
        VAR(head) = 0;
        VAR(tail) = 0;
        pthread_mutex_init(&mtx, 0);
        pthread_cond_init(&cv, 0);
    }
    
    ~queue_t()
    {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&cv);
    }
    
    void enqueue(void* data)
    {
        node_t* n = new node_t;
        n->VAR(next) = 0;
        n->VAR(data) = data;
        bool was_empty = false;
        
        pthread_mutex_lock(&mtx);
        if (VAR(head) == 0)
        {
            was_empty = true;
            VAR(head) = n;
            VAR(tail) = n;
        }
        else
        {
            VAR(tail)->VAR(next) = n;
            VAR(tail) = n;
        }
        pthread_mutex_unlock(&mtx);
        
        if (was_empty)
            pthread_cond_broadcast(&cv);
    }
    
    void* dequeue()
    {
        node_t* n = 0;
        
        pthread_mutex_lock(&mtx);
        while (VAR(head) == 0)
            pthread_cond_wait(&cv, &mtx);
        n = VAR(head);
        if (n->VAR(next) == 0)
            VAR(tail) = 0;
        VAR(head) = n->VAR(next);
        pthread_mutex_unlock(&mtx);
        
        void* data = n->VAR(data);
        delete n;
        return data;
    }
    
private:
    struct node_t
    {
        VAR_T(node_t*) next;
        VAR_T(void*) data;
    };
    
    VAR_T(node_t*) head;
    VAR_T(node_t*) tail;
    
    pthread_mutex_t mtx;
    pthread_cond_t cv;
};

void* enqueue_thread(void* ctx)
{
    queue_t* q = static_cast<queue_t*>(ctx);
    for (size_t i = 0; i != 4; i += 1)
        q->enqueue((void*)(i + 1));
    return 0;
}

void* dequeue_thread(void* ctx)
{
    queue_t* q = static_cast<queue_t*>(ctx);
    for (size_t i = 0; i != 4; i += 1)
    {
        void* data = q->dequeue();
        assert((int)(uintptr_t)data >= 1 && (int)(uintptr_t)data <= 4);
    }
    return 0;
}

void queue_test()
{
    queue_t q;
    
    pthread_t th [4];
    for (size_t i = 0; i != 2; i += 1)
        pthread_create(&th[i], 0, enqueue_thread, &q);
    for (size_t i = 2; i != 4; i += 1)
        pthread_create(&th[i], 0, dequeue_thread, &q);
    
    void* res = 0;
    for (size_t i = 0; i != 4; i += 1)
        pthread_join(th[i], &res);
}

int main()
{
    rl::test_params p;
    p.iteration_count = 100000;
    //p.search_type = rl::sched_full;
    //p.context_bound = 5;
    //p.execution_depth_limit = 200;
    rl::execute<queue_test, 4>(p);
}
