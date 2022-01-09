#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For gethostbyname */
#include <arpa/inet.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
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

#include "message.cpp"

#if 0
#define debug_print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) 0
#endif

class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;
  bool disable = false;
  std::string m_title;

public:
  Gauge() {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
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
};

int cfg_loop_times = 5;
int cfg_send_size = 128;
int cfg_total_threads = 4;
int cfg_conn_in_thread = 1;

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"lt", required_argument, nullptr, 0},
      {"size", required_argument, nullptr, 0},
      {"total", required_argument, nullptr, 0},
      {"ct", required_argument, nullptr, 0},
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
            cfg_loop_times = std::stoi(optarg);
            break;

          case 1:
            cfg_send_size = std::stoi(optarg);
            break;

          case 2:
            cfg_total_threads = std::stoi(optarg);
            break;

          case 3:
            cfg_conn_in_thread = std::stoi(optarg);
            break;
        }

        break;
      default:
        debug_print("unknown arg: %c", c);
        fail = true;
    }
  }

  if (fail) {
    return false;
  }

  return true;
}

struct ConnectionContext {
  int status = 0;
  sockaddr_in udp_dest;
  int fd;
};

void new_udp_event(evutil_socket_t fd, short what, void *arg) {
  ConnectionContext *conn_ctx = (ConnectionContext *)arg;
  if (what & EV_READ) {
    MessageHeader header;
    auto [re_buf, re_size] = msg_cast(header);
    socklen_t len = sizeof(sockaddr_in);
    int read_n = recvfrom(fd, re_buf, re_size, MSG_PEEK,
                          (sockaddr *)&conn_ctx->udp_dest, &len);

    if (header.type != MessageType::ClientReply) {
      uint8_t drain_buf[64];
      recvfrom(fd, drain_buf, 0, 0, (sockaddr *)&conn_ctx->udp_dest, &len);
      debug_print("error: reply not ClientReply\n");
    } else {
      ClientReplyMessage reply;
      auto [buf, size] = msg_cast(reply);
      read_n =
          recvfrom(fd, buf, size, 0, (sockaddr *)&conn_ctx->udp_dest, &len);
      debug_print("drain reply: %d\n", reply.data);
    }
    conn_ctx->status = 0;
  } else {
    debug_print("unhandled event 0x%x\n", what);
  }
}

int send_multiple(const char *server_ip, int server_port) {
  event_base *base = event_base_new();

  struct sockaddr_in udp_dest;
  int res = 0;

  memset(&udp_dest, 0, sizeof(udp_dest));
  udp_dest.sin_family = AF_INET;
  udp_dest.sin_addr.s_addr = inet_addr(server_ip);
  udp_dest.sin_port = htons(server_port);

  std::vector<event *> udp_conns;
  std::vector<ConnectionContext *> conn_ctxs;
  for (int i = 0; i < cfg_conn_in_thread; i++) {
    ConnectionContext *conn_ctx = new ConnectionContext;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    conn_ctx->fd = fd;
    conn_ctx->udp_dest = udp_dest;
    event *ev =
        event_new(base, fd, EV_READ | EV_PERSIST, new_udp_event, conn_ctx);
    udp_conns.push_back(ev);
    conn_ctxs.push_back(conn_ctx);
    event_add(ev, nullptr);
  }

  ClientMessage msg;
  msg.client_id = 5;
  auto [buf, size] = msg_cast(msg);

  int send_count = 0;
  bool send_all = false;
  while (true) {
    for (auto conn : conn_ctxs) {
      if (conn->status == 0 && send_count < cfg_loop_times) {
        res = sendto(conn->fd, buf, size, 0, (sockaddr *)&udp_dest,
                     sizeof(udp_dest));
        if (res == -1) {
          debug_print("send error: %d, %s\n", errno, strerror(errno));
          return res;
        }
        debug_print("send to %s:%d size %d\n", inet_ntoa(udp_dest.sin_addr),
               ntohs(udp_dest.sin_port), res);
        conn->status = 1;
        send_count += 1;
        if (send_count >= cfg_loop_times) {
          send_all = true;
        }
      }
      debug_print("send_count = %d\n", send_count);
    }

    event_base_loop(base, EVLOOP_ONCE);

    bool all_idle = false;
    int idle_count = 0;
    for (auto conn : conn_ctxs) {
      if (conn->status == 0) {
        idle_count += 1;
      }
    }
    debug_print("idle count: %d, conn_ctxs.size(): %zu\n", idle_count, conn_ctxs.size());
    if (idle_count == conn_ctxs.size()) {
      all_idle = true;
    }
    debug_print("all_idle: %d, send_all: %d\n", all_idle, send_all);
    if (all_idle && send_all) {
      debug_print("finish all msg, break loop\n");
      break;
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  if (!parse_cmd(argc, argv)) {
    return 1;
  }

  const char *server_ip = "127.0.0.1";
  int server_port = 40713;

  const char *cp;
  int fd;

  int res = 0;

  /* Allocate a new socket */
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  std::vector<std::thread> threads;
  threads.reserve(cfg_total_threads);

  Gauge gauge;
  gauge.set_probe1();
  for (int i = 0; i < cfg_total_threads; i++) {
    threads.emplace_back(send_multiple, server_ip, server_port);
  }

  for (int i = 0; i < cfg_total_threads; i++) {
    threads[i].join();
  }
  gauge.set_probe2();
  gauge.total_time_ms(cfg_total_threads * cfg_conn_in_thread * cfg_loop_times);

  // send_multiple(fd, server_ip, server_port);
}