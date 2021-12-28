#include <chrono>
#include <iostream>
#include <vector>

int main() {
  using namespace std::chrono;
  using clock = steady_clock;

  std::vector<clock::time_point> v;
  v.reserve(100);
  for (int i = 0; i < 1000; i++) v.push_back(clock::now());

  for (size_t i = 0; i < v.size() - 1; i++) {
    std::cout << duration_cast<nanoseconds>(v[i + 1] - v[i]).count() << "ns\n";
  }
}