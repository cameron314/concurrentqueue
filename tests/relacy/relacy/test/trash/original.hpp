Here is a recent version of the revised pc_sample.c which uses inline x86
ASM and compiles under VC++ (I am planning on coding the entire thing in
pure assembly language):
____________________________________________________________________
#if ! defined(PC_SAMPLE_INCLUDE_H)
#   define PC_SAMPLE_INCLUDE_H
#   pragma warning(push)
#   pragma warning (disable : 4100 4505 4706)
#   if defined(__cplusplus)
      extern "C" {
#   endif
/*===========================================================*/

/* Very Simple x86 Atomic Operations API & Implmentation
_____________________________________________________________*/
typedef __int32 atomicword;
typedef atomicword volatile* const atomicword_pthis;

static int
x86_DWCASPTR(
 void volatile* const,
 void* const,
 void const* const
);

static atomicword
x86_XADDWORD(
 atomicword_pthis,
 atomicword const
);

static atomicword
x86_XCHGWORD(
 atomicword_pthis,
 atomicword const
);

__declspec(naked) int
x86_DWCASPTR(
 void volatile* const _pthis,
 void* const pcmp,
 void const* const pxhcg
) {
  _asm {
    PUSH ESI
    PUSH EBX
    MOV ESI, [ESP + 16]
    MOV EAX, [ESI]
    MOV EDX, [ESI + 4]
    MOV ESI, [ESP + 20]
    MOV EBX, [ESI]
    MOV ECX, [ESI + 4]
    MOV ESI, [ESP + 12]
    LOCK CMPXCHG8B QWORD PTR [ESI]
    JNE x86_DWCASPTR_failed
    MOV EAX, 1
    POP EBX
    POP ESI
    RET

x86_DWCASPTR_failed:
    MOV ESI, [ESP + 16]
    MOV [ESI], EAX
    MOV [ESI + 4], EDX
    MOV EAX, 0
    POP EBX
    POP ESI
    RET
  }

}

__declspec(naked) atomicword
x86_XADDWORD(
 atomicword_pthis _pthis,
 atomicword const value
) {
  _asm {
    MOV EDX, [ESP + 4]
    MOV EAX, [ESP + 8]
    LOCK XADD [EDX], EAX
    RET
  }

}

__declspec(naked) atomicword
x86_XCHGWORD(
 atomicword_pthis _pthis,
 atomicword const value
) {
  _asm {
    MOV EDX, [ESP + 4]
    MOV EAX, [ESP + 8]
    XCHG [EDX], EAX
    RET
  }

}

#define x86_XCHGPTR(mp_pdest, mp_src) ( \
  (void*)x86_XCHGWORD( \
    ((atomicword_pthis)(mp_pdest)), \
    ((atomicword const)(mp_src)) \
  ) \
)

#define XCHGWORD x86_XCHGWORD
#define XCHGPTR x86_XCHGPTR
#define XADDWORD x86_XADDWORD
#define DWCASPTR x86_DWCASPTR

/* Proxy-Collector API & Implmentation (Revisited)  ;^)
   Inventor: Chris M. Thomasson
_____________________________________________________________*/
#include <stddef.h>
#include <assert.h>
#if ! defined(NDEBUG)
# include <stdio.h>
#endif

#define CONTAINER_OF(mp_this, mp_type, mp_member) ( \
  (mp_type*)(((unsigned char*)(mp_this)) - \
  offsetof(mp_type, mp_member)) \
)

typedef struct pc_region_s pc_region, pc_node;
typedef struct pc_master_s pc_master;
typedef void (pc_fp_dtor) (pc_node*);
typedef struct pc_sys_anchor_s pc_sys_anchor;

struct pc_sys_anchor_s {
  atomicword refcnt;
  pc_region* region;

};

struct pc_region_s {
  pc_sys_anchor next;
  pc_node* defer;

};

struct pc_master_s {
  pc_sys_anchor head;
  pc_region region;
  pc_fp_dtor* fp_dtor;
};

#define PC_MASTER_STATICINIT(mp_this, mp_fp_dtor) { \
  { 0, &(mp_this)->region }, \
  { { 0, NULL }, NULL }, (mp_fp_dtor) \

}

static void
pc_sys_dtor(
 pc_master* const,
 pc_region* const
);

static void
pc_init(
 pc_master* const,
 pc_fp_dtor* const
);

static void
pc_node_init(
 pc_node* const
);

static void
pc_node_link(
 pc_node* const,
 pc_node* const
);

static pc_region*
pc_acquire(
 pc_master* const
);

static void
pc_release(
 pc_master* const,
 pc_region* const
);

static void
pc_defer(
 pc_region* const,
 pc_node* const
);

static void
pc_mutate(
 pc_master* const,
 pc_node* const
);

void
pc_init(
 pc_master* const _this,
 pc_fp_dtor* const fp_dtor
) {
  pc_master src = { { 0 } };
  *_this = src;
  _this->head.region = &_this->region;
  _this->fp_dtor = fp_dtor;

}

pc_region*
pc_acquire(
 pc_master* const _this
) {
  pc_sys_anchor cmp = _this->head, xchg;
  do {
    xchg.refcnt = cmp.refcnt + 2;
    xchg.region = cmp.region;
  } while (! DWCASPTR(&_this->head, &cmp, &xchg));
  return cmp.region;

}

void
pc_release(
 pc_master* const _this,
 pc_region* const region
) {
  if (XADDWORD(&region->next.refcnt, -2) == 3) {
    pc_sys_dtor(_this, region);
  }

}

void
pc_node_init(
 pc_node* const _this
) {
  pc_node src = { { 0 } };
  *_this = src;

}

void
pc_node_link(
 pc_node* const _this,
 pc_node* const next
) {
  _this->defer = next;

}

void
pc_defer(
 pc_region* const _this,
 pc_node* const node
) {
  node->defer = XCHGPTR(&_this->defer, node);

}

void
pc_mutate(
 pc_master* const _this,
 pc_node* const node
) {
  pc_sys_anchor cmp = _this->head, xchg = { 0 };
  node->next.refcnt = 2;
  node->next.region = NULL;
  xchg.region = node;
  while (! DWCASPTR(&_this->head, &cmp, &xchg));
  cmp.region->next.region = node;
  if (XADDWORD(&cmp.region->next.refcnt,
               cmp.refcnt + 1) == -cmp.refcnt) {
    pc_sys_dtor(_this, cmp.region);
  }

}

void
pc_sys_dtor(
 pc_master* const _this,
 pc_region* const region
) {
  int dtors = 0, reset = 0;
  pc_region* head = region;
  pc_region* tail = region;
  pc_region* next = region->next.region;

  while (next) {
    if (XADDWORD(&next->next.refcnt, -2) != 3) {
      break;
    }
    tail = next;
    next = next->next.region;
  }

  tail->next.region = NULL;
  while (head) {
    pc_region* const next = head->next.region;
    pc_node* defer = head->defer;
    assert(head->next.refcnt == 1);
    if (head != &_this->region) {
      head->defer = defer;
      defer = head;
    } else {
      reset = 1;
    }
    while (defer) {
      pc_node* const next = defer->defer;
      _this->fp_dtor(defer);
      ++dtors;
      defer = next;
    }
    head = next;
  }

  if (reset) {
    _this->region.defer = NULL;
    pc_mutate(_this, &_this->region);
  }

#if ! defined(NDEBUG)
  {
    static atomicword g_pc_sys_epoch = 0;
    atomicword const epoch = XADDWORD(&g_pc_sys_epoch, 1);
    if (dtors) {
      printf("pc_sys_dtor::epoch/dtors(%d/%d)\n",
             epoch, dtors);
    }
  }
#endif

}

/*===========================================================*/
#   if defined(__cplusplus)
      }
#   endif
#   pragma warning(pop)
#endif
____________________________________________________________________


 struct foo_node {
  foo_node* next;
  pc_node pcn;

};

struct foo_list {
  foo_node* head;
  pc_master pc;

};

static foo_list g_list = {
  NULL, PC_MASTER_STATICINIT()

};

void foo_node_dtor(pc_node* pcn) {
  foo_node* const _this = container_of(pcn, foo_node, pcn);
  free(_this);

}

void foo_reader() {
  int i;
  foo_node* node;
  pc_region* pcr = pc_acquire(&g_list.pc);
  for (i = 1 ;; ++i) {
    node = LOAD_DEPENDS(&g_list.head);
    while (node) {
      foo_node* const next = LOAD_MBDEPEND(&node->next);
      [...];
      node = next;
    }
    if (! (i % 1000)) {
      pc_release(&g_list.pc, pcr);
      pcr = pc_acquire(&g_list.pc);
    }
  }
  pc_release(&g_list.pc, pcr);

}

void foo_writer() {
  int i;
  foo_node* node, *cmp;
  pc_region* pcr = pc_acquire(&g_list.pc);
  for (i = 1 ;; ++i) {
    if (i % 10) {
      node = malloc(sizeof(*node));
      if (node) {
        foo_node* cmp;
        pc_node_init(node, NULL, foo_node_dtor);
        cmp = g_list.head;
        do {
          node->next = cmp;
        } while (! CASIBM_MBREL(&g_list.head, &cmp, node));
      }
    } else {
      node = g_list.head;
      do {
        if (! node) { break; }
      } while (! CASIBM_MBACQ(&g_list.head, &node, node->next));
      if (node) {
        if (! (i % 20)) {
          pc_mutate(&g_list.pc, &node->pcn);
        } else {
          pc_defer(pcr, &node->pcn);
        }
      }
    }
    if (! (i % 500)) {
      pc_release(&g_list.pc, pcr);
      pcr = pc_acquire(&g_list.pc);
    }
  }
  pc_release(&g_list.pc, pcr);
} 





1. Region 1 is current
2. Thread 1 acquires region 1
3. Thread 2 executes pc_mutate()
4. Region 2 is current
5. Thread 3 acquires region 2
6. Thread 3 loads pointer to node 1
7. Thread 1 removes node 1 from data structure
8. Thread 1 executes pc_defer() and defers node 1 to region 1
9. Thread 1 releases region 1
10. Dtor executed for region 1, node 1 is deleted
11. Thread 3 accesses node 1
12. Bang! 


