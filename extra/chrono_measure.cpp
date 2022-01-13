#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>
#include <functional>
#include <sstream>
#include <atomic>
#include <tuple>

#include <msgpack.hpp>

using namespace std::chrono;

// ------ Note: statistics may shift drastically on different platform (VM, WSL, Bare Metal ...)

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 718.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 560.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 684.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 671.000000 ns
*/
void base_chrono() {
  auto start = steady_clock::now();
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
`calling(nullptr);` brings 200 ns more latency
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 828.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 845.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 748.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 928.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 859.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 969.000000 ns

asm of `if` branch:
  mov     rax, QWORD PTR [rbp-40]
  mov     esi, 0
  mov     rdi, rax
  call    bool std::operator!=<void>(std::function<void ()> const&, decltype(nullptr))
  test    al, al
  je      .L13
*/
void calling(std::function<void()> func) {
  auto start = steady_clock::now();
  if (func != nullptr) {
    func();
  }
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 898.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1200.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1593.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1076.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 999.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1074.000000 ns

asm:
  lea     rax, [rbp-64]
  mov     rdi, rax
  call    std::mutex::lock()
*/
void mutex_lock() {
  std::mutex mtx;
  auto start = steady_clock::now();
  mtx.lock();
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
  mtx.unlock();
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1028.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 980.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 925.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1102.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 840.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 854.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1022.000000 ns

asm:
  lea     rax, [rbp-64]
  mov     rdi, rax
  call    std::mutex::unlock()
*/
void mutex_unlock() {
  std::mutex mtx;
  mtx.lock();
  auto start = steady_clock::now();
  mtx.unlock();
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1015.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1147.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1129.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1119.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1241.000000 ns

Maybe a 100 ns overhead on lock_guard initialization
*/
void mutex_lock_guard() {
  std::mutex mtx;
  auto start = steady_clock::now();
  std::lock_guard lock(mtx);
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 1893.000000 ns
thread 2 mutex takes 167755.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 1500.000000 ns
thread 2 mutex takes 105165.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 1507.000000 ns
thread 2 mutex takes 137012.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 1635.000000 ns
thread 2 mutex takes 153918.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 1872.000000 ns
thread 2 mutex takes 199353.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 mutex takes 2288.000000 ns
thread 2 mutex takes 161294.000000 ns
*/
void multi_thread_mutex() {
  std::mutex mtx;

  auto func = [&mtx](int i){
    auto start = steady_clock::now();
    std::lock_guard lock(mtx);
    auto end = steady_clock::now();

    double diff = (end - start).count();
    printf("thread %d mutex takes %f ns\n", i, diff);
  };
  std::thread t1(func, 1);
  std::thread t2(func, 2);

  t1.join();
  t2.join();
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 820.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 699.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 720.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 833.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 770.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 776.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 655.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 645.000000 ns

asm
  lea     rax, [rbp-20]
  mov     esi, 0
  mov     rdi, rax
  call    std::__atomic_base<unsigned int>::operator++(int)

Faster than mutex lock
*/
void atomic_add() {
  std::atomic<uint32_t> acc(0);
  auto start = steady_clock::now();
  acc++;
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 799.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 704.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 620.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 716.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 731.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 653.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 702.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 667.000000 ns

asm
  mov     eax, DWORD PTR [rbp-20]
  mov     esi, 65535
  mov     edi, eax
  call    std::operator&(std::memory_order, std::__memory_order_modifier)
  mov     DWORD PTR [rbp-24], eax
  lea     rax, [rbp-36]
  mov     eax, DWORD PTR [rax]
  nop
  mov     DWORD PTR [rbp-4], eax
*/
void atomic_load() {
  std::atomic<uint32_t> acc(0);
  acc++;
  auto start = steady_clock::now();
  uint32_t num = acc.load();
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 140972.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 60250.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 56240.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 77646.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 34580.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 25050.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 44244.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 39064.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 17222.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
thread 1 waiting...
takes 47737.000000 ns

time is not stable
*/
void multi_thread_switch() {
  std::mutex mtx;

  std::vector<steady_clock::time_point> probes;
  probes.reserve(2);
  std::atomic<uint32_t> acc(0);

  auto func = [&mtx, &probes, &acc](int i){
    acc++;
    while (acc.load() != 2) {
      printf("thread %d waiting...\n", i);
    }
    std::lock_guard lock(mtx);
    probes.emplace_back(steady_clock::now());

  };
  std::thread t1(func, 1);
  std::thread t2(func, 2);

  t1.join();
  t2.join();

  steady_clock::time_point start;
  steady_clock::time_point end;
  if (probes[0] > probes[1]) {
    start = probes[1];
    end = probes[0];
  } else {
    start = probes[0];
    end = probes[1];
  }

  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
new_buffer(1000);
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 9229.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 10760.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 9493.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 9360.000000 ns

new_buffer(10000);
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 8934.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 12343.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 9506.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 10629.000000 ns
*/
void new_buffer(size_t size) {
  auto start = steady_clock::now();
  char *buffer = new char[size];
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);

  delete []buffer;
}


void consecutive_new_buffer(size_t size) {
  auto start = steady_clock::now();
  char *buffer = new char[size];
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);

  delete []buffer;

  start = steady_clock::now();
  buffer = new char[size];
  end = steady_clock::now();
  diff = (end - start).count();
  printf("takes %f ns\n", diff);

  delete []buffer;

  start = steady_clock::now();
  buffer = new char[size];
  end = steady_clock::now();
  diff = (end - start).count();
  printf("takes %f ns\n", diff);

  delete []buffer;

}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 19600.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 17982.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 62566.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 13017.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 10907.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 20595.000000 ns

asm of std::string is a bunch of code:
  lea     rax, [rbp-33]
  mov     rdi, rax
  call    std::allocator<char>::allocator() [complete object constructor]
  lea     rdx, [rbp-33]
  lea     rax, [rbp-80]
  mov     esi, OFFSET FLAT:.LC0
  mov     rdi, rax
  call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string(char const*, std::allocator<char> const&) [complete object constructor]
  lea     rax, [rbp-80]
  mov     rdi, rax
  call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::~basic_string() [complete object destructor]
  lea     rax, [rbp-33]
  mov     rdi, rax
  call    std::allocator<char>::~allocator() [complete object destructor]
*/
void use_string() {
  auto start = steady_clock::now();
  std::string("hello world");
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 20942.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 14620.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 21136.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 13606.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 22352.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 13649.000000 ns

So locality is working
*/
void loop_string() {
  auto start = steady_clock::now();
  for (int i = 0; i < 10; i++) {
    std::string("hello world");
  }
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 19000.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11605.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 18649.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 14212.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11884.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11817.000000 ns
*/
void access_string(std::string &str) {
  auto start = steady_clock::now();
  sockaddr_in addr;
  addr.sin_addr.s_addr = inet_addr(str.c_str());
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1004.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1080.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1093.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1145.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 996.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 850.000000 ns

asm of `const char *target = str.c_str();`
  mov     rax, QWORD PTR [rbp-56]
  mov     rdi, rax
  call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::c_str() const
  mov     QWORD PTR [rbp-8], rax
*/
void access_string_v2(std::string &str) {
  auto start = steady_clock::now();
  const char *target = str.c_str();
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1485.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1416.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1393.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1153.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1345.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 1312.000000 ns

asm of memcpy:
  mov     rax, QWORD PTR [rbp-1096]
  mov     rdi, rax
  call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::size() const
  mov     rbx, rax
  mov     rax, QWORD PTR [rbp-1096]
  mov     rdi, rax
  call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::c_str() const
  mov     rcx, rax
  lea     rax, [rbp-1072]
  mov     rdx, rbx
  mov     rsi, rcx
  mov     rdi, rax
  call    memcpy
*/
void copy_string(std::string &str) {
  auto start = steady_clock::now();
  char buf[1024];
  memcpy(buf, str.c_str(), str.size());
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 20901.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11589.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 22893.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 21221.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 22502.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 24606.000000 ns

serialization add 20 us overhead on memcpy
*/
struct Message {
  uint64_t data1;
  uint64_t data2;

  MSGPACK_DEFINE(data1, data2);
};
void msgpack_simple_message() {
  auto start = steady_clock::now();

  Message msg;
  msg.data1 = 10;
  msg.data2 = 20;
  std::stringstream stream;
  msgpack::pack(stream, msg);
  char buf[1024];
  memcpy(buf, stream.str().c_str(), stream.str().size());

  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}

/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11784.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 11052.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 10268.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 13835.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 10431.000000 ns

asm
  lea     rax, [rbp-64]
  mov     rdi, rax
  call    std::vector<unsigned char, std::allocator<unsigned char> >::vector() [complete object constructor]
  mov     rdx, QWORD PTR [rbp-96]
  lea     rax, [rbp-64]
  mov     rsi, rdx
  mov     rdi, rax
  call    std::vector<unsigned char, std::allocator<unsigned char> >::reserve(unsigned long)
  lea     rax, [rbp-64]
  mov     rdi, rax
  call    std::vector<unsigned char, std::allocator<unsigned char> >::begin()
  mov     rdx, rax
  mov     rcx, QWORD PTR [rbp-88]
  mov     rax, QWORD PTR [rbp-96]
  add     rcx, rax
  mov     rax, QWORD PTR [rbp-88]
  mov     rsi, rcx
  mov     rdi, rax
  call    __gnu_cxx::__normal_iterator<unsigned char*, std::vector<unsigned char, std::allocator<unsigned char> > > std::copy<unsigned char*, __gnu_cxx::__normal_iterator<unsigned char*, std::vector<unsigned char, std::allocator<unsigned char> > > >(unsigned char*, unsigned char*, __gnu_cxx::__normal_iterator<unsigned char*, std::vector<unsigned char, std::allocator<unsigned char> > >)

Constructing vector needs operator new(), which brings overhead
*/
void copy_to_vector(uint8_t *buf, size_t size) {
  auto start = steady_clock::now();

  std::vector<uint8_t> data;
  data.reserve(size);
  std::copy(buf, buf + size, data.begin());

  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
yijian@kvs-backup:~/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 747.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 771.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 473.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 664.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 664.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 548.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 663.000000 ns
yijian@kvs-backup:~/morphling-c/extra$ ./a.out
takes 566.000000 ns

asm of casting:
  lea     rax, [rbp-64]
  mov     QWORD PTR [rbp-8], rax
*/
void message_type_casting() {
  auto start = steady_clock::now();
  Message msg;
  msg.data1 = 10;
  msg.data2 = 20;
  uint8_t *msg_buf = reinterpret_cast<uint8_t *>(&msg);
  size_t msg_size = sizeof(msg);
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
}


/*
On WSL, base measurement case is 0~100 ns
about 400 ns more than `message_type_casting`,
and changing `pair` tp `tuple` make things worse.
ucc@desktop-8sjudi:~/code/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 600.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 600.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 400.000000 ns

-O3 drop the assignment line, because there are useless variables.
ucc@desktop-8sjudi:~/code/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O3
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
*/
template <typename T>
inline std::pair<uint8_t*, size_t> msg_cast(T &msg) {
  return {reinterpret_cast<uint8_t *>(&msg), sizeof(msg)};
}
void inline_message_casting() {
  auto start = steady_clock::now();
  Message msg;
  msg.data1 = 10;
  msg.data2 = 20;
  auto [msg_buf, msg_size] = msg_cast(msg);
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
  printf("msg 1: 0x%lx\n", *(uint64_t *)msg_buf);
}

/*
ucc@desktop-8sjudi:~/code/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O0
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 200.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 200.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 300.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 300.000000 ns
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns

asm
  lea     rax, [rbp-48]
  mov     rdi, rax
  call    MsgBufInfo msg_cast2<Message>(Message&)
  mov     QWORD PTR [rbp-64], rax
  mov     QWORD PTR [rbp-56], rdx
  call    std::chrono::_V2::steady_clock::now()
  mov     QWORD PTR [rbp-72], rax

It seems inline function doesn't work in -O0

ucc@desktop-8sjudi:~/code/morphling-c/extra$ g++ chrono_measure.cpp -pthread -std=c++17 -O3
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
msg 1: 0xa
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
msg 1: 0xa
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
msg 1: 0xa
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
msg 1: 0xa
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 100.000000 ns
msg 1: 0xa
ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./a.out
takes 0.000000 ns
msg 1: 0xa
*/
struct MsgBufInfo {
  uint8_t *msg_buf;
  size_t msg_size;
};
template <typename T>
inline MsgBufInfo msg_cast2(T &msg) {
  return {reinterpret_cast<uint8_t *>(&msg), sizeof(msg)};
}
void inline_message_casting2() {
  auto start = steady_clock::now();
  Message msg;
  msg.data1 = 10;
  msg.data2 = 20;
  auto [msg_buf, msg_size] = msg_cast2(msg);
  auto end = steady_clock::now();
  double diff = (end - start).count();
  printf("takes %f ns\n", diff);
  printf("msg 1: 0x%lx\n", *(uint64_t *)msg_buf);
}

std::string ip("192.168.5.29");
uint8_t g_buf[1024];

int main() {
  calling(nullptr);
}