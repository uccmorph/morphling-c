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

class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;
  bool disable = false;
  std::string m_title;
  size_t m_last_calculate_idx = 0;

public:
  Gauge() {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
  }

  Gauge(const std::string title): m_title(title) {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
  }

  bool is_disable() {
    return disable;
  }

  size_t set_probe1() {
    if (disable) {
      return 0;
    }
    measure_probe1.emplace_back(std::chrono::steady_clock::now());
    return measure_probe1.size() - 1;
  }

  size_t set_probe2() {
    if (disable) {
      return 0;
    }
    measure_probe2.emplace_back(std::chrono::steady_clock::now());
    return measure_probe2.size() - 1;
  }

  void set_disable(bool b) {
    disable = b;
  }

  void total_time_ms(int total_msg) {
    if (disable) {
      return;
    }
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
    printf("total time: %f ms, throughput: %f Kops\n", res, total_msg / res);
  }

  void index_time_us() {
    if (disable) {
      return;
    }
    if (measure_probe1.size() != measure_probe2.size()) {
      LOG_F(ERROR, "Two probe don't have same number of data (%zu : %zu)",
            measure_probe1.size(), measure_probe2.size());
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

  void instant_time_us() {
    if (disable) {
      return;
    }
    if (measure_probe1.size() != measure_probe2.size()) {
      LOG_F(ERROR, "Two probe don't have same number of data (%zu : %zu)",
            measure_probe1.size(), measure_probe2.size());
      return;
    }
    auto &start = *(measure_probe1.rbegin());
    auto &end = *(measure_probe2.rbegin());
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-3;
    printf("[%s] loop %zu, interval: %f us\n", m_title.c_str(), measure_probe1.size(), res);
  }

  void instant_time_us(size_t idx) {
    if (disable) {
      return;
    }

    auto &start = measure_probe1[idx];
    auto &end = measure_probe2[idx];
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-3;
    printf("[%s] loop %zu, interval: %f us\n", m_title.c_str(), idx, res);
  }

  void average_time_us() {
    if (disable) {
      return;
    }
    size_t probes = measure_probe2.size() - m_last_calculate_idx;
    double acc = 0.0;
    double max = 0.0;
    double min = 1e9;

    for (size_t i = m_last_calculate_idx; i < measure_probe2.size(); i++) {
      auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      measure_probe2[i] - measure_probe1[i])
                      .count();
      diff *= 1e-3;
      acc += diff;
      if (max < diff) {
        max = diff;
      }
      if (diff < min) {
        min = diff;
      }
    }
    if (probes > 0) {
      double time = acc / probes;
      printf("[%s] %zu points, average time: %f us, min: %f us, max: %f us\n", m_title.c_str(), probes, time, min, max);
      // printf("[%s] min: %f us, max: %f us\n", m_title.c_str(), min, max);
    } else {
      // printf("[%s] no probe points in these time\n", m_title.c_str());
    }
    m_last_calculate_idx = measure_probe2.size();
  }
};

#endif