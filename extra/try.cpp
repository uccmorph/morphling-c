#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>

void vector_init() {
  uint8_t *buf = new uint8_t[16];
  memset(buf, 'a', 16);
  std::vector<uint8_t> data(buf, buf + 16);

  for (auto c : data) {
    printf("0x%x ", c);
  }
  printf("\n");
}


void map_init_on_assign() {
  uint8_t *buf = new uint8_t[16];
  memset(buf, 'a', 16);

  std::unordered_map<int, std::vector<uint8_t>> container;

  container[1].assign(buf, buf + 16);

  auto &data = container[1];
  for (auto c : data) {
    printf("0x%x ", c);
  }
  printf("\n");
}

int main() {
  map_init_on_assign();
}