#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For gethostbyname */
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <cassert>

using namespace std::chrono;

int cfg_loop_count = 10000;
std::string dest_ip("127.0.0.1");

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"lc", required_argument, nullptr, 0},
      {"dest", required_argument, nullptr, 0},
      {0, 0, 0, 0}};

  int c = 0;
  bool fail = false;
  int mandatory = 0;
  while (!fail) {
    int option_index = 0;
    c = getopt_long(argc, argv, "", long_options, &option_index);
    if (c == -1) break;
    switch (c) {
      case 0:
        switch (option_index) {
          case 0:
            cfg_loop_count = std::stoi(optarg);
            break;

          case 1:
            dest_ip = optarg;
            break;

        }

        break;
      default:
        printf("unknown arg: %c\n", c);
        fail = true;
    }
  }

  if (fail) {
    return false;
  }

  return true;
}

class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;
  bool disable = false;
  std::string m_title;
  size_t m_last_calculate_idx = 0;
  std::vector<double> calculate_helper;

public:
  Gauge() {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
    calculate_helper.reserve(1000000);
  }

  Gauge(const std::string title): m_title(title) {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
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
      printf("no metrics");
      return;
    }
    auto &start = *(measure_probe1.begin());
    auto &end = *(measure_probe2.rbegin());
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-6;
    printf("msg: %d, total time: %f ms, throughput: %f Kops\n", total_msg, res, total_msg / res);
  }

  void index_time_us() {
    if (disable) {
      return;
    }
    if (measure_probe1.size() != measure_probe2.size()) {
      printf("Two probe don't have same number of data (%zu : %zu)",
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
      printf("Two probe don't have same number of data (%zu : %zu)",
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
      printf("[%s] %zu points, average time: %f us\n", m_title.c_str(), probes, time);
      printf("[%s] min: %f us, max: %f us\n", m_title.c_str(), min, max);
    } else {
      printf("[%s] no probe points in these time\n", m_title.c_str());
    }
    m_last_calculate_idx = measure_probe2.size();
  }
};

int main(int argc, char **argv) {
  parse_cmd(argc, argv);

  int server_port = 40713;

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  Gauge gauge;
  struct sockaddr_in udp_dest;
  memset(&udp_dest, 0, sizeof(udp_dest));
  udp_dest.sin_family = AF_INET;
  udp_dest.sin_addr.s_addr = inet_addr(dest_ip.c_str());
  udp_dest.sin_port = htons(server_port);

  char buf[1024];

  for (int i = 0; i < cfg_loop_count; i++) {
    gauge.set_probe1();
    int send_n = sendto(fd, buf, 1024, 0, (sockaddr *)&udp_dest, sizeof(udp_dest));
    gauge.set_probe2();
    assert(send_n == 1024);
  }

  gauge.average_time_us();
}