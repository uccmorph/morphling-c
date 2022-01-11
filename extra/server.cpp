#include <assert.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <chrono>

#include "message.cpp"

#if 0
#define debug_print(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) 0
#endif

void new_udp_event(evutil_socket_t fd, short what, void *arg);

class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;
  bool disable = false;
  std::string m_title;
  int m_report_interval = 10000; // calculate average latency every 10000 msgs

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

  void average_time_us() {
    if (disable) {
      return;
    }

  }
};

class UDPDestination {
  int m_fd;
  sockaddr_in m_addr;
  socklen_t m_len;

public:
  int peer_id = -1;

  UDPDestination(int fd, sockaddr_in &addr, socklen_t len):
    m_fd(fd), m_addr(addr), m_len(len) {}

  UDPDestination(int fd, sockaddr_in &addr):
    m_fd(fd), m_addr(addr), m_len(sizeof(sockaddr_in)) {}

  void send(uint8_t *buf, size_t size) {
    int send_n = sendto(m_fd, buf, size, 0, (sockaddr *)&m_addr, m_len);
    if (send_n != size) {
      debug_print("send size %d is not equal to `size %zu`\n", send_n, size);
    }
  }
};

Gauge g_client_handler_gauge;

class UDPServer {
public:
  struct PeerInfo {
    std::string ip;
    int port;
    sockaddr_in sock_addr;
    int fd;
  };

private:
  int m_sock_fd;
  event_base *m_base;
  std::unordered_map<
      int, std::function<void(Message &, std::unique_ptr<UDPDestination> &)>>
      m_callbacks;
  int m_me;
  uint64_t m_seq = 0;
  std::vector<bool> m_seq_finished;

  std::unordered_map<int, PeerInfo> m_peer_info;
  std::unordered_map<int, std::unique_ptr<UDPDestination>> m_pending_client;

private:
  bool init_udp(int port);

  void on_client_msg(ClientMessage &, std::unique_ptr<UDPDestination> &dest);
  void on_nested_msg(NestedMessage &, std::unique_ptr<UDPDestination> &dest);
  void on_nested_reply_msg(NestedReplyMessage &, std::unique_ptr<UDPDestination> &dest);

public:
  UDPServer();
  bool init_base_service();
  void set_info(int self, std::unordered_map<int, std::string> &peer_addr,
                std::unordered_map<int, int> &peer_port);

  void start();
  void handle_msg(int fd);
  void bcast_msg(Message &msg);
  void send(uint8_t *buf, size_t size, std::unique_ptr<UDPDestination> dest);

  void register_cb(int type, std::function<void ()> cb);
  void register_cb(
      int type, std::function<void(Message &, std::unique_ptr<UDPDestination> &)> cb);
};

bool UDPServer::init_udp(int port) {
  struct sockaddr_in sin;
  int socklen = sizeof(sin);
  memset(&sin, 0, socklen);

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(port);

  if (bind(m_sock_fd, (sockaddr *)&sin, sizeof(sin)) == -1) {
    debug_print("bind error: %d, %s\n", errno, strerror(errno));
    return false;
  }

  return true;
}

void UDPServer::on_client_msg(ClientMessage &msg, std::unique_ptr<UDPDestination> &dest) {
  debug_print("recv ClientMessage from client: %d\n", msg.client_id);
  g_client_handler_gauge.set_probe1();
  m_pending_client[m_seq] = std::move(dest);
  for (auto &peer : m_peer_info) {
    auto id = peer.first;
    if (id == m_me) {
      continue;
    }
    auto &info = peer.second;

    UDPDestination dest(m_sock_fd, info.sock_addr);
    NestedMessage nest;
    nest.data = m_seq;
    auto [buf, size] = msg_cast(nest);
    dest.send(buf, size);
  }
  m_seq += 1;
  auto probe_idx = g_client_handler_gauge.set_probe2();
  g_client_handler_gauge.instant_time_us(probe_idx);
}

void UDPServer::on_nested_msg(NestedMessage &msg, std::unique_ptr<UDPDestination> &dest) {
  debug_print("recv NestedMessage %d from %d\n", msg.data, dest->peer_id);

  NestedReplyMessage reply;
  reply.data = msg.data;
  auto [buf, size] = msg_cast(reply);
  dest->send(buf, size);
}

void UDPServer::on_nested_reply_msg(NestedReplyMessage &msg, std::unique_ptr<UDPDestination> &dest) {
  debug_print("recv NestedReplyMessage %d from %d\n", msg.data, dest->peer_id);

  if (!m_seq_finished[msg.data]) {
    m_seq_finished[msg.data] = true;
    ClientReplyMessage creply;
    creply.data = msg.data;
    auto [buf, size] = msg_cast(creply);
    m_pending_client[msg.data]->send(buf, size);
    m_pending_client.erase(msg.data);
  }
}

UDPServer::UDPServer() {
  m_base = event_base_new();
  m_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

  m_pending_client.reserve(1000000);
  m_seq_finished.resize(1000000, false);

  debug_print("base sock fd: %d\n", m_sock_fd);
}

bool UDPServer::init_base_service() {

  auto port = m_peer_info[m_me].port;
  if (!init_udp(port)) {
    return false;
  }

  int ev_flag = EV_READ | EV_PERSIST;
  event *l_ev = event_new(m_base, m_sock_fd, ev_flag, new_udp_event, this);
  event_add(l_ev, nullptr);

  register_cb(
      MessageType::Client,
      [this](Message &msg, std::unique_ptr<UDPDestination> &dest) {
        ClientMessage &cmsg = reinterpret_cast<ClientMessage &>(msg);
        this->on_client_msg(cmsg, dest);
      });
  register_cb(
      MessageType::Nested,
      [this](Message &msg, std::unique_ptr<UDPDestination> &dest) {
        NestedMessage &nmsg = reinterpret_cast<NestedMessage &>(msg);
        this->on_nested_msg(nmsg, dest);
      });
  register_cb(
      MessageType::NestedReply,
      [this](Message &msg, std::unique_ptr<UDPDestination> &dest) {
        NestedReplyMessage &nrmsg = reinterpret_cast<NestedReplyMessage &>(msg);
        this->on_nested_reply_msg(nrmsg, dest);
      });

  return true;
}

void UDPServer::set_info(int self,
                         std::unordered_map<int, std::string> &peer_addr,
                         std::unordered_map<int, int> &peer_port) {
  m_me = self;

  for (auto peer : peer_addr) {
    auto id = peer.first;
    m_peer_info[id] = UDPServer::PeerInfo();
    m_peer_info[id].ip = peer_addr[id];
    m_peer_info[id].port = peer_port[id];

    sockaddr_in &addr = m_peer_info[id].sock_addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(peer.second.c_str());
    addr.sin_port = htons(peer_port[id]);

    m_peer_info[id].fd = socket(AF_INET, SOCK_DGRAM, 0);
  }
}

void UDPServer::start() {
  event_base_dispatch(m_base);
}

void UDPServer::handle_msg(int fd) {
  sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  // if we don't read it out, then it may keep notifying EV_READ
  // but, after setting EV_ET, then it doesn't matter if we don't read

  MessageHeader header;
  auto [buf, size] = msg_cast(header);
  int read_n = recvfrom(fd, buf, size, MSG_PEEK, (sockaddr *)&addr, &addr_len);
  // debug_print("read %d bytes from %s:%d\n", read_n, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
  // debug_print("header, size: %u, type: %u\n", header.size, header.type);

  auto find_itr = m_callbacks.find((int)header.type);
  if (find_itr == m_callbacks.end()) {
    debug_print("type %u is not register\n", header.type);
    uint8_t drain_buf[8];

    sleep(1);
    read_n = recvfrom(fd, drain_buf, 0, 0, (sockaddr *)&addr, &addr_len);
    debug_print("drain %d bytes, want %zu\n", read_n, sizeof(MessageHeader));

    auto reply = msg_buf_cast<MessageHeader>(drain_buf, sizeof(MessageHeader));
    debug_print("reply size: %u, type: %u\n", reply->size, reply->type);
    return;
  }


  std::unique_ptr<UDPDestination> dest = std::make_unique<UDPDestination>(fd, addr);
  for (auto &peer : m_peer_info) {
    auto &info = peer.second;
    std::string ip(inet_ntoa(addr.sin_addr));
    auto port = ntohs(addr.sin_port);
    if (ip == info.ip && port == info.port) {
      dest->peer_id = peer.first;
    }
  }
  if (header.type == MessageType::Client) {
    ClientMessage msg;

    auto [buf, size] = msg_cast(msg);
    int read_n = recvfrom(fd, buf, size, 0, (sockaddr *)&addr, &addr_len);
    if (read_n != size) {
      debug_print("read size %d is not equal to size %zu\n", read_n, size);
    }

    m_callbacks[(int)header.type](msg, dest);
  }
  if (header.type == MessageType::Nested) {
    NestedMessage nest;
    auto [buf, size] = msg_cast(nest);
    int read_n = recvfrom(fd, buf, size, 0, (sockaddr *)&addr, &addr_len);
    if (read_n != size) {
      debug_print("read size %d is not equal to size %zu\n", read_n, size);
    }

    m_callbacks[(int)header.type](nest, dest);
  }
  if (header.type == MessageType::NestedReply) {
    NestedReplyMessage msg;
    auto [buf, size] = msg_cast(msg);
    int read_n = recvfrom(fd, buf, size, 0, (sockaddr *)&addr, &addr_len);
    if (read_n != size) {
      debug_print("read size %d is not equal to size %zu\n", read_n, size);
    }

    m_callbacks[(int)header.type](msg, dest);
  }
}

void UDPServer::bcast_msg(Message &msg) {

}

void UDPServer::register_cb(
    int type, std::function<void(Message &, std::unique_ptr<UDPDestination> &)> cb) {
  m_callbacks[type] = cb;
}

/* ========================================================== */

void new_udp_event(evutil_socket_t fd, short what, void *arg) {
  UDPServer *server = reinterpret_cast<UDPServer *>(arg);
  if (what & EV_READ) {
    server->handle_msg(fd);
  } else {
    debug_print("unhandled event 0x%x\n", what);
  }
}

void parse_addr(const char *cmd_arg, std::vector<std::string> &peers_addr) {
  std::string arg(cmd_arg);
  size_t pos = 0;
  size_t old_pos = pos;
  while (true) {
    pos = arg.find(",", old_pos);
    if (pos == std::string::npos) {
      peers_addr.push_back(arg.substr(old_pos));
      break;
    } else {
      peers_addr.push_back(arg.substr(old_pos, pos - old_pos));
      old_pos = pos + 1;
    }
  }
}

int cfg_id = 0;

bool parse_cmd(int argc, char **argv) {
int has_default = 0;
  static struct option long_options[] = {
      {"id", required_argument, nullptr, 0},
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
            cfg_id = std::stoi(optarg);
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

int main(int argc, char **argv) {
  if (!parse_cmd(argc, argv)) {
    return 1;
  }


  std::unordered_map<int, std::string> peer_addr;
  peer_addr[0] = "127.0.0.1";
  peer_addr[1] = "127.0.0.1";
  peer_addr[2] = "127.0.0.1";

  std::unordered_map<int, int> peer_port;
  peer_port[0] = 40713;
  peer_port[1] = 40714;
  peer_port[2] = 40715;

  UDPServer server;
  server.set_info(cfg_id, peer_addr, peer_port);
  server.init_base_service();

  server.start();

  // udp_echo_loop(40713);
}