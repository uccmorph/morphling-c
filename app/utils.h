#ifndef __APP_UTILS_H__
#define __APP_UTILS_H__

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include <loguru.hpp>

#include "smr.h"
#include "message.h"

void parse_addr(const char *cmd_arg, std::vector<std::string> &peers_addr);
void pack_operation(Operation &op, ClientMessage &msg, uint8_t *dest, size_t &size);
void pack_get_guiddance(uint8_t *dest, size_t &size);
void full_read(struct bufferevent *bev, uint8_t *buf, size_t size);
void drain_read(struct bufferevent *bev, uint8_t *buf, size_t size);
MessageType recv_msg(struct bufferevent *bev, uint8_t *buf, size_t &size);

class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;

public:
  Gauge() {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
  }

  void set_probe1() {
    measure_probe1.emplace_back(std::chrono::steady_clock::now());
  }

  void set_probe2() {
    measure_probe2.emplace_back(std::chrono::steady_clock::now());
  }

  void total_time_ms() {
    if (measure_probe1.size() == 0 || measure_probe2.size() == 0) {
      LOG_F(ERROR, "no metrics");
      return;
    }
    auto &start = *(measure_probe1.begin());
    auto &end = *(measure_probe2.rbegin());
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-6;
    printf("total time: %f ms\n", res);
  }

  void index_time_us() {
    if (measure_probe1.size() != measure_probe2.size()) {
      LOG_F(ERROR, "Two probe don't have same number of data");
      return;
    }
    for (size_t i = 0; i < measure_probe1.size(); i++) {
      double interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           measure_probe2[i] - measure_probe1[i])
                           .count();
      interval *= 1e-3;
      printf("loop %zu, interval: %f us\n", i, interval);
    }
  }
};

#endif