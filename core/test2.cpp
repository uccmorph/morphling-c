#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>

#include "smr.h"

int main() {
  std::vector<Entry> log;
  log.reserve(10);
  std::string str("hello world");
  std::vector<uint8_t> data(str.begin(), str.end());
  printf("====== 1\n");
  log.emplace_back(data.size(), data);
  printf("====== 2\n");
  log.emplace_back(data.size(), data);
  printf("====== 3\n");

  printf("log size: %zu\n", log.size());

  SMRLog m_log;
  printf("====== 4\n");
  m_log.append(log[0]);
  printf("====== 5\n");
  m_log.append(log[1]);
  m_log.append(log[1]);
  m_log.show_all();
  m_log.truncate(1);
  printf("====== 6\n");
  m_log.show_all();
  printf("====== 7\n");
}