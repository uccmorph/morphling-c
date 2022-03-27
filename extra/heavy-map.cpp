#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <chrono>

using clocking = std::chrono::steady_clock;

std::unordered_map<uint64_t, std::vector<uint8_t>> storage;
std::vector<std::vector<uint8_t>> result;
const int data_num = 1000000;
std::vector<uint8_t> data;

int parseLine(char* line){
    // This assumes that a digit will be found and the line ends in " Kb".
    int i = strlen(line);
    const char* p = line;
    while (*p <'0' || *p > '9') p++;
    line[i-3] = '\0';
    i = atoi(p);
    return i;
}

int getValue(){ //Note: this value is in KB!
    FILE* file = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL){
        if (strncmp(line, "VmSize:", 7) == 0){
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    return result;
}

void init_data() {
  result.reserve(data_num);
  data.assign(1000, 0xa0);
}

void load_workload() {
  for (uint64_t i = 0; i < data_num; i++) {

    storage.emplace(i, data);
  }
}

void read_load() {
  for (uint64_t i = 0; i < data_num; i++) {

    auto &value = storage.at(i);
    result.push_back(value);
  }
}

int main() {
  auto start = clocking::now();
  load_workload();
  auto finish_load = clocking::now();

  printf("load takes: %ld us\n", (finish_load-start).count());
  printf("mem usage: %d KB\n", getValue());

  auto start_read = clocking::now();
  read_load();
  auto finish_read = clocking::now();
  auto read_takes = (finish_read-start_read).count();
  double each_takes = read_takes / data_num;
  printf("read takes: %ld us, each %lf us\n", read_takes, each_takes);
}