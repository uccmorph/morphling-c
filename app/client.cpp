/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <string>
#include <chrono>

#include <msgpack.hpp>
#include <loguru.hpp>
#include "message.h"
#include "smr.h"

#include "utils.cpp"

int cfg_port = 9990;
std::string cfg_ip;
int cfg_send_nums = 0;
bool cfg_is_read = false;
int cfg_msg_size = 1000;

std::vector<std::chrono::steady_clock::time_point> measure_probe1;
std::vector<std::chrono::steady_clock::time_point> measure_probe2;
std::vector<std::chrono::steady_clock::time_point> measure_probe3;

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"ip", required_argument, nullptr, 0},
      {"port", required_argument, nullptr, 0},
      {"nums", required_argument, nullptr, 0},
      {"ro", no_argument, &has_default, 0},
      {"ms", required_argument, nullptr, 0},
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
            cfg_ip = optarg;
            LOG_F(1, "ip = %s", cfg_ip.c_str());
            mandatory += 1;
            break;
          case 1:
            cfg_port = std::stoi(optarg);
            LOG_F(1, "port = %d", cfg_port);
            mandatory += 1;
            break;
          case 2:
            cfg_send_nums = std::stoi(optarg);
            LOG_F(1, "total number %d", cfg_send_nums);
            mandatory += 1;
            break;
          case 3:
            cfg_is_read = true;
            LOG_F(1, "enable read only");
            break;
          case 4:
            cfg_msg_size = std::stoi(optarg);
            LOG_F(1, "payload data size: %d", cfg_msg_size);
        }

        break;
      default:
        LOG_F(ERROR, "unknown arg: %c", c);
        fail = true;

    }
  }


  if (fail || mandatory != 3) {
    LOG_F(ERROR, "need --ip, --port, --nums");
    return false;
  }
  return true;
}

std::string default_data(int size) {
  std::string res;
  for (int i = 0; i < size; i++) {
    res.append("a");
  }
  return res;
}

int send_loop(int fd, std::string &query) {
  measure_probe1.emplace_back(std::chrono::steady_clock::now());
  char buf[2048];
  Operation op{
    .op_type = cfg_is_read ? 0 : 1,
    .key_hash = 0x5499,
    .data = std::vector<uint8_t>(query.begin(), query.end()),
  };
  ClientMessage msg{
    .epoch = 1,
    .key_hash = 0x5499,
  };

  size_t total_size = 0;
  pack_operation(op, msg, (uint8_t *)buf, total_size);

  int n = send(fd, buf, total_size, 0);
  measure_probe2.emplace_back(std::chrono::steady_clock::now());
  // n = send(fd, msg_buf.c_str(), msg_buf.size(), 0);
  LOG_F(3, "send real length: %zu", n);

  memset(buf, 0, 1024);
  int readn = recv(fd, (void *)buf, 1024, 0);
  LOG_F(3, "receive %d bytes: %s", readn, buf);
  measure_probe3.emplace_back(std::chrono::steady_clock::now());
}

int run() {
  const char *server_ip = cfg_ip.c_str();
  struct sockaddr_in sin;
  int fd;

  int res = 0;

  /* Allocate a new socket */
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  /* Connect to the remote host. */
  sin.sin_family = AF_INET;
  sin.sin_port = htons(cfg_port);
  res = inet_aton(server_ip, &sin.sin_addr);
  if (connect(fd, (struct sockaddr *)&sin, sizeof(sin))) {
    perror("connect");
    close(fd);
    return 1;
  }

  measure_probe1.reserve(cfg_send_nums);
  measure_probe2.reserve(cfg_send_nums);
  measure_probe3.reserve(cfg_send_nums);

  std::string query = default_data(cfg_msg_size);
  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

  for (int i = 0; i < cfg_send_nums; i++) {
    send_loop(fd, query);
  }
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  double diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
  diff *= 1e-9;

  LOG_F(INFO, "total test time: %f s", diff);

  double interval1_avg = 0.0;
  double interval2_avg = 0.0;
  for (int i = 0; i < cfg_send_nums; i++) {
    double interval1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           measure_probe2[i] - measure_probe1[i])
                           .count();
    double interval2 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           measure_probe3[i] - measure_probe2[i])
                           .count();
    interval1 *= 1e-3;
    interval2 *= 1e-3;
    LOG_F(INFO, "send loop %d, interval1 = %f us, interval2 = %f us", i, interval1, interval2);
    interval1_avg += interval1;
    interval2_avg += interval2;
  }

  LOG_F(INFO, "interval1 avg: %f us, interval2 avg: %f us", interval1_avg/cfg_send_nums, interval2_avg/cfg_send_nums);

  close(fd);

  return 0;
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }

  run();
}