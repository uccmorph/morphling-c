#include <unistd.h>

#include <iostream>
#include <thread>
#include <cstring>
#include <atomic>

constexpr size_t _DataSize = 100000;

struct Guidance {
  int flags;
  unsigned int data[_DataSize];
};

Guidance guide;

void modify() {
  int count = 0;

  count += 1;
  // 1. set flag to 1
  guide.flags = 1;
  // std::atomic_thread_fence(std::memory_order_release);

  // 2. modify data
  for (size_t i = 0; i < _DataSize; i++) {
    guide.data[i] = count;
  }

  // 3. set flag to 0
  guide.flags = 0;
  // std::atomic_thread_fence(std::memory_order_release);

}

void fetch() {
  while (true) {
    // std::atomic_thread_fence(std::memory_order_acquire);
    auto state = guide.flags;
    if (state != 0) {
      printf("guidance is not ready, %d\n", guide.data[_DataSize-1]);
    } else {
      printf("finish, %d\n", guide.data[_DataSize-1]);
      return;
    }
  }
}

void foo1() {
  // sleep(2);
  for (size_t i = 0; i < 10000000; i++) {}
  guide.flags = 1;
  std::atomic_thread_fence(std::memory_order_release);
}

void foo2() {
  std::atomic_thread_fence(std::memory_order_acquire);
  auto state = guide.flags;
  printf("enter %s, state = %d\n", __PRETTY_FUNCTION__, state);
}

void init_guidance() {
  guide.flags = 1;
  memset(guide.data, 0, sizeof(guide.data));
}


// g++ -std=c++17 -pthread modify_guidance.cpp
/*
It seems without fence, fetch thread can still read consistently. Like:
...
guidance is not ready, 0
guidance is not ready, 0
guidance is not ready, 0
guidance is not ready, 0
guidance is not ready, 0
finish, 1
 */
int main(int argc, char **argv) {
  init_guidance();
  std::thread t1(modify);
  std::thread t2(fetch);

  t1.join();
  t2.join();

  guide.flags = 10;

  printf("guidance: %p, size: %zu\n", &guide, sizeof(Guidance));

  int * tmp = (int *)&guide;
  for (size_t i = 0; i < 10; i++) {
    printf("%08x ", tmp[i]);
  }
  printf("\n");

  int &tmp2 = *(int *)&guide;
  tmp2 = 20;

  for (size_t i = 0; i < 10; i++) {
    printf("%08x ", tmp[i]);
  }
  printf("\n");
}