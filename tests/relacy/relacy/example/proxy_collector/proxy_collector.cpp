#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"


struct pc_sys_anchor;
struct pc_region;
struct pc_master;
typedef pc_region pc_node;
typedef void (pc_fp_dtor) (pc_region*);
typedef unsigned __int64 uint64_t;

struct pc_sys_anchor
{
    int refcnt;
    pc_region* region;

    pc_sys_anchor()
    {
    }

    pc_sys_anchor(int rc, pc_region* r = 0)
    {
        refcnt = rc;
        region = r;
    }

    bool operator == (pc_sys_anchor const& right) const
    {
        return refcnt == right.refcnt
            && region == right.region;
    }

    pc_sys_anchor operator + (pc_sys_anchor const& right) const
    {
        pc_sys_anchor res;
        res.refcnt = refcnt + right.refcnt;
        res.region = (pc_region*)((intptr_t)region + (intptr_t)right.region);
        return res;
    }

    pc_sys_anchor operator - (pc_sys_anchor const& right) const
    {
        pc_sys_anchor res;
        res.refcnt = refcnt - right.refcnt;
        res.region = (pc_region*)((intptr_t)region - (intptr_t)right.region);
        return res;
    }
};

std::ostream& operator << (std::ostream& s, pc_sys_anchor const& right)
{
    return s << "{" << right.refcnt << "," << right.region << "}";
}

struct pc_region
{
    std::atomic<pc_sys_anchor> next;
    std::atomic<pc_region*> defer;

    pc_region()
    {
        next($) = pc_sys_anchor(0, 0);
        defer($) = 0;
    }

    void link(pc_region* next)
    {
        defer.store(next, rl::memory_order_relaxed);
    }

    void defer_node(pc_region* node)
    {
        pc_region* region = defer.exchange(node, rl::memory_order_release);
        node->defer.store(region, rl::memory_order_relaxed);
    }
};

struct pc_master
{
    std::atomic<pc_sys_anchor> head;
    pc_region stub_region;
    pc_fp_dtor* fp_dtor;

    pc_master(pc_fp_dtor* const dtor)
    {
        pc_sys_anchor src (0, &stub_region);
        head.store(src, rl::memory_order_relaxed);
        fp_dtor = dtor;
    }

    pc_region* acquire()
    {
        pc_sys_anchor cmp (head.load(rl::memory_order_relaxed));
        pc_sys_anchor xchg;
        do
        {
            xchg.refcnt = cmp.refcnt + 2;
            xchg.region = cmp.region;
        }
        while (false == head.compare_exchange_weak(cmp, xchg, rl::memory_order_acquire));
        return cmp.region;
    }

    void release(pc_region* region)
    {
        pc_sys_anchor prev = region->next.fetch_sub(2, rl::memory_order_acq_rel);
        if (prev.refcnt == 3)
            sys_dtor(region);
    }

    void mutate(pc_region* node)
    {
        pc_sys_anchor src (2, 0);
        node->next.store(src, rl::memory_order_relaxed);
        pc_sys_anchor xchg (0, node);
        pc_sys_anchor cmp = head.load(rl::memory_order_relaxed);
        while (false == head.compare_exchange_weak(cmp, xchg, std::memory_order_acq_rel));

        pc_sys_anchor cmp2 = cmp.region->next.load(rl::memory_order_relaxed);
        pc_sys_anchor xchg2;
        do
        {
            xchg2 = pc_sys_anchor(cmp2.refcnt, node);
        }
        while (false == cmp.region->next.compare_exchange_weak(cmp2, xchg2, rl::memory_order_release));

        pc_sys_anchor prev = cmp.region->next.fetch_add(cmp.refcnt + 1, rl::memory_order_acq_rel);
        if (prev.refcnt == -cmp.refcnt)
            sys_dtor(cmp.region);
    }

    void sys_dtor(pc_region* region)
    {
        int reset = 0;
        pc_region* head = region;
        pc_region* tail = region;
        pc_sys_anchor nx = region->next.load(rl::memory_order_relaxed);
        pc_region* next = nx.region;

        while (next)
        {
            pc_sys_anchor prev = next->next.fetch_sub(2, rl::memory_order_acq_rel);
            if (prev.refcnt != 3)
                break;
            tail = next;
            nx = next->next.load(rl::memory_order_relaxed);
            next = nx.region;
        }

        nx = tail->next.load(rl::memory_order_relaxed);
        nx.region = 0;
        tail->next.store(nx, rl::memory_order_relaxed);

        while (head)
        {
            nx = head->next.load(rl::memory_order_relaxed);
            pc_region* const next = nx.region;

            pc_region* defer = head->defer.load(rl::memory_order_relaxed);

            nx = head->next.load(rl::memory_order_relaxed);
            RL_ASSERT(nx.refcnt == 1);

            if (head != &stub_region)
            {
                head->defer.store(defer, rl::memory_order_relaxed);
                defer = head;
            }
            else
            {
                reset = 1;
            }

            while (defer)
            {
                pc_region* const next = defer->defer.load(rl::memory_order_relaxed);
                fp_dtor(defer);
                defer = next;
            }
            head = next;
        }

        if (reset)
        {
            stub_region.defer.store(0, rl::memory_order_relaxed);
            mutate(&stub_region);
        }
    }
};




struct foo_node
{
    pc_node pcn;
    std::atomic<foo_node*> next;
    rl::var<int> data;
};

void foo_node_dtor(pc_node* pcn)
{
    // yes, very fragile
    foo_node* const n = (foo_node*)pcn;
    delete n;
}

struct foo_list
{
    std::atomic<foo_node*> head;
    pc_master pc;

    foo_list()
        : head(0)
        , pc(foo_node_dtor)
    {
    }
};

struct proxy_collector_test : rl::test_suite<proxy_collector_test, 4>
{
    foo_list m_list;

    void before()
    {
        m_list.head($) = 0;
    }

    void after()
    {
        foo_node* node = new foo_node;
        m_list.pc.mutate(&node->pcn);
    }

    void thread(unsigned index)
    {
        if (index < 2)
        {
            pc_region* pcr = m_list.pc.acquire();
            for (int i = 0; i != 4; ++i)
            {
                foo_node* node = m_list.head.load(rl::memory_order_acquire);
                while (node)
                {
                    foo_node* const next = node->next.load(rl::memory_order_acquire);
                    intptr_t volatile data = node->data($);
                    (void)data;
                    node = next;
                }
                if (2 == i)
                {
                    m_list.pc.release(pcr);
                    pcr = m_list.pc.acquire();
                }
            }
            m_list.pc.release(pcr);
        }
        else
        {
            pc_region* pcr = m_list.pc.acquire();
            for (int i = 0; i != 4; ++i)
            {
                if (0 == (i % 2))
                {
                    foo_node* node = new foo_node;
                    node->data($) = 1;
                    foo_node* cmp = m_list.head.load(rl::memory_order_relaxed);
                    do
                    {
                        node->next.store(cmp, rl::memory_order_relaxed);
                    }
                    while (false == m_list.head.compare_exchange_weak(cmp, node, rl::memory_order_release));
                }
                else
                {
                    foo_node* node = m_list.head.load(rl::memory_order_acquire);
                    foo_node* next;
                    do
                    {
                        if (0 == node)
                            break;
                        next = node->next.load(rl::memory_order_relaxed);
                    }
                    while (false == m_list.head.compare_exchange_weak(node, next, rl::memory_order_acquire));

                    if (node)
                    {
                        //if (1 == i)
                        {
                            m_list.pc.mutate(&node->pcn);
                        }
                        //else
                        //{
                        //    pcr->defer_node(&node->pcn);
                        //}
                    }
                }
                if (i % 2)
                {
                    m_list.pc.release(pcr);
                    pcr = m_list.pc.acquire();
                }
            }
            m_list.pc.release(pcr);
        }
    }
};




int main()
{
    rl::test_params params;
    params.iteration_count = 1000;
    rl::simulate<proxy_collector_test>(params);
}

