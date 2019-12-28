#include "../relacy/relacy_std.hpp"

struct test : rl::test_suite<test, 4> {
  std::atomic<int> x_;
  std::atomic<int> y_;
  int r2x, r2y, r3x, r3y;

  void before() {
    x_.store(0, std::memory_order_relaxed);
    y_.store(0, std::memory_order_relaxed);
    r2x = r2y = r3x = r3y = 0;
  }

  void thread(unsigned thread_index) {
    switch (thread_index) {
      case 0:;
        x_.store(1, std::memory_order_relaxed);
        break;
      case 1:
        y_.store(1, std::memory_order_relaxed);
        break;
      case 2:
        r2x = x_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        r2y = y_.load(std::memory_order_relaxed);
        break;
      case 3:
        r3y = y_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        r3x = x_.load(std::memory_order_relaxed);
        break;
    }
  }

  void after() {
    // This assert should fire according to C++ memory model,
    // however it does not in the current relacy implementation.
    RL_ASSERT(!(r2x == 1 && r3y == 1 && r2y == 0 && r3x == 0));
  }
};

int main() {
  rl::test_params p;
  p.iteration_count = 1000000;
  rl::simulate<test>(p);
}
