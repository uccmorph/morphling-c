#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <cstring>
#include <chrono>
#include <loguru.hpp>
#include <unordered_map>
#include <thread>

#include "morphling.h"
#include "transport.h"
#include "utils.h"

Gauge g_gauge_client_msg("Client op");
Gauge g_gauge_send("udp send");

int cfg_id = 0;
std::vector<std::string> cfg_peers_addr;
std::vector<int> service_port(3);
std::vector<int> peer_port(3);
std::vector<int> guidance_port(3);

bool parse_cmd(int argc, char **argv) {
  static struct option long_options[] = {{"id", required_argument, nullptr, 0},
                                         {"peers-addr", required_argument, nullptr, 0},
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
            LOG_F(1, "id = %s", optarg);
            mandatory += 1;
            cfg_id = std::stoi(optarg);
            break;

          case 1:
            LOG_F(1, "peers: %s", optarg);
            mandatory += 1;
            parse_addr(optarg, cfg_peers_addr);
            break;
        }

        break;
      default:
        fail = true;
        LOG_F(ERROR, "unknown arg: %c", c);
    }
  }
  if (fail || mandatory != 2) {
    LOG_F(ERROR, "needs args: --id");
    LOG_F(ERROR,
          "needs args: --peers-addr, a comma separated string, like "
          "<ip1>,<ip2>,<ip3>");
    LOG_F(ERROR, "current peers' ports are hard coded: 9990, 9991 and 9992");
    return false;
  }

  LOG_F(1, "cfg_id = %d", cfg_id);
  for (size_t i = 0; i < cfg_peers_addr.size(); i++) {
    LOG_F(1, "cfg_peers_addr[%d] = %s", (int)i, cfg_peers_addr[i].c_str());
  }

  return true;
}

class UDPTransport : public Transport {
  int m_fd;
  sockaddr_in m_addr;
  bool m_is_built = false;

 public:
  UDPTransport(int fd, sockaddr_in addr) : m_fd(fd), m_addr(addr) {}
  ~UDPTransport() {}
  void send(uint8_t *buf, uint64_t size);
  bool recv(uint8_t *recv_buf, uint64_t max_size);
  bool is_ready();

  void set_built();
};

void UDPTransport::send(uint8_t *buf, uint64_t size) {
  g_gauge_send.set_probe1();
  int send_n = sendto(m_fd, buf, size, 0, (sockaddr *)&m_addr, sizeof(sockaddr_in));
  if (send_n == -1) {
    LOG_F(FATAL, "udp send error: %d, %s", errno, std::strerror(errno));
  }
  assert(send_n == (int)size);
  LOG_F(INFO, "fd %d send send_n = %d bytes", m_fd, send_n);
  g_gauge_send.set_probe2();
}

bool UDPTransport::recv(uint8_t *recv_buf, uint64_t expect_size) {
  socklen_t addr_len = sizeof(m_addr);
  int read_n = recvfrom(m_fd, recv_buf, expect_size, 0, (sockaddr *)&m_addr, &addr_len);
  assert(read_n == (int)expect_size);
  assert(addr_len == sizeof(m_addr));
  LOG_F(INFO, "recv read_n = %d bytes", read_n);
  return true;
}

bool UDPTransport::is_ready() { return true; }

void UDPTransport::set_built() {
  m_is_built = true;
}

class UDPServer {
 public:
  struct PeerInfo {
    std::string ip;
    int peer_port;
    int service_port;
    int guidance_port;

    sockaddr_in sock_addr;
    int peer_fd;
  };

 private:
  event_base *m_base;
  int m_service_socket;
  int m_peer_socket;
  int m_guidance_socket;
  std::vector<PeerInfo> m_peer_info;
  int m_me;
  std::unordered_map<int, std::unique_ptr<Transport>> m_peer_trans;

  void base_init();
 public:
  Morphling *replica;

 public:
  UDPServer();
  UDPServer(Morphling &r);
  void set_peer_info();
  bool init_udp(int fd, int port);
  void init_service();
  void init_guidance_service();
  void start();
  void on_udp_event();
  MessageHeader recv_header(int fd, sockaddr_in &addr);

  std::vector<PeerInfo> &get_peer_info();
  int get_id();
};

void UDPServer::base_init() {
  m_base = event_base_new();
  m_peer_socket = socket(AF_INET, SOCK_DGRAM, 0);
  m_service_socket = socket(AF_INET, SOCK_DGRAM, 0);
  m_guidance_socket = socket(AF_INET, SOCK_DGRAM, 0);
}

UDPServer::UDPServer() {
  base_init();
}

UDPServer::UDPServer(Morphling &r): replica(&r) {
  base_init();
  set_peer_info();
}

void UDPServer::set_peer_info() {
  m_peer_info.resize(cfg_peers_addr.size());
  m_me = cfg_id;
  for (size_t id = 0; id < cfg_peers_addr.size(); id++) {
    m_peer_info[id].ip = cfg_peers_addr[id];
    m_peer_info[id].service_port = service_port[id];
    m_peer_info[id].peer_port = peer_port[id];
    m_peer_info[id].guidance_port = guidance_port[id];

    if ((int)id != m_me) {
      sockaddr_in &addr = m_peer_info[id].sock_addr;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = inet_addr(cfg_peers_addr[id].c_str());
      addr.sin_port = htons(peer_port[id]);
      m_peer_info[id].peer_fd = socket(AF_INET, SOCK_DGRAM, 0);
      m_peer_trans[id] = std::make_unique<UDPTransport>(m_peer_info[id].peer_fd, addr);
      TransPtr trans = std::make_unique<UDPTransport>(m_peer_info[id].peer_fd, addr);
      replica->set_peer_trans(id, trans);
    }
  }
}

bool UDPServer::init_udp(int fd, int port) {
  struct sockaddr_in sin;
  int socklen = sizeof(sin);
  memset(&sin, 0, socklen);

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(port);

  if (bind(fd, (sockaddr *)&sin, sizeof(sin)) == -1) {
    LOG_F(ERROR, "bind error: %d, %s", errno, strerror(errno));
    return false;
  }

  return true;
}

MessageHeader UDPServer::recv_header(int fd, sockaddr_in &addr) {
  socklen_t addr_len = sizeof(addr);
  MessageHeader header;
  uint8_t *buf = reinterpret_cast<uint8_t *>(&header);
  int read_n = recvfrom(fd, buf, sizeof(header), MSG_PEEK, (sockaddr *)&addr, &addr_len);
  assert(read_n == (int)sizeof(header));
  assert(addr_len == sizeof(addr));

  return header;
}

void UDPServer::init_service() {
  init_udp(m_peer_socket, m_peer_info[m_me].peer_port);
  init_udp(m_service_socket, m_peer_info[m_me].service_port);

  int flag = EV_READ | EV_PERSIST;
  event *ev_service = event_new(
      m_base, m_service_socket, flag,
      [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        LOG_F(INFO, "new client event");
        if (what & EV_READ) {
          g_gauge_client_msg.set_probe1();

          sockaddr_in addr;
          MessageHeader header = server->recv_header(fd, addr);

          std::unique_ptr<Transport> trans = std::make_unique<UDPTransport>(fd, addr);

          if (header.type != MessageType::MsgTypeClient &&
              header.type != MessageType::MsgTypeGetGuidance) {
            LOG_F(ERROR, "can not handle msg type %d from clients", header.type);
          }
          server->replica->handle_message(header, trans);

          g_gauge_client_msg.set_probe2();
        }
      },
      this);
  event_add(ev_service, nullptr);

  event *ev_peer = event_new(
      m_base, m_peer_socket, flag,
      [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        if (what & EV_READ) {
          sockaddr_in addr;
          MessageHeader header = server->recv_header(fd, addr);

          if (header.type != MessageType::MsgTypeAppend &&
              header.type != MessageType::MsgTypeAppendReply &&
              header.type != MessageType::MsgTypeGuidance &&
              header.type != MessageType::MsgTypeGetGuidance) {
            LOG_F(ERROR, "can not handle msg type %d from peers", header.type);
          }
          std::unique_ptr<Transport> trans = std::make_unique<UDPTransport>(fd, addr);
          server->replica->handle_message(header, trans);
        }
      },
      this);
  event_add(ev_peer, nullptr);

  event *ev_period = event_new(
      m_base, -1, EV_PERSIST,
      [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        auto &peer_info = server->get_peer_info();
        for (size_t id = 0; id < peer_info.size(); id++) {
          if ((int)id == server->get_id()) {
            continue;
          }
          std::unique_ptr<Transport> trans =
              std::make_unique<UDPTransport>(peer_info[id].peer_fd, peer_info[id].sock_addr);
          server->replica->reply_guidance(trans);
        }
      },
      this);
  struct timeval one_sec = {5, 0};
  event_add(ev_period, &one_sec);

  event *ev_latency_gauge = event_new(
      m_base, -1, EV_PERSIST,
      [](evutil_socket_t fd, short what, void *arg) {
        g_gauge_client_msg.average_time_us();
        g_gauge_send.average_time_us();
      },
      this);
  struct timeval gauge_interval = {1, 0};
  event_add(ev_latency_gauge, &gauge_interval);
}

void UDPServer::init_guidance_service() {
  init_udp(m_guidance_socket, m_peer_info[m_me].guidance_port);
  int flag = EV_READ | EV_PERSIST;
  event *ev_guidance = event_new(
      m_base, m_guidance_socket, flag,
      [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        LOG_F(INFO, "new get guidance event");
        if (what & EV_READ) {
          sockaddr_in addr;
          MessageHeader header = server->recv_header(fd, addr);

          std::unique_ptr<Transport> trans = std::make_unique<UDPTransport>(fd, addr);

          if (header.type != MessageType::MsgTypeGetGuidance) {
            LOG_F(ERROR, "can not handle msg type %d from clients", header.type);
          }
          server->replica->handle_message(header, trans);
        }
      },
      this);
  event_add(ev_guidance, nullptr);
}

void UDPServer::start() { event_base_dispatch(m_base); }

std::vector<UDPServer::PeerInfo> &UDPServer::get_peer_info() { return m_peer_info; }

int UDPServer::get_id() { return m_me; }

/* --------- threading --------- */

void guidance_service(Morphling &r) {
  UDPServer server(r);

  server.init_guidance_service();
  server.start();
}


int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);

  std::vector<int> peers{0, 1, 2};

  service_port[0] = 9990;
  service_port[1] = 9991;
  service_port[2] = 9992;

  peer_port[0] = 9993;
  peer_port[1] = 9994;
  peer_port[2] = 9995;

  guidance_port[0] = 9996;
  guidance_port[1] = 9997;
  guidance_port[2] = 9998;


  Morphling replica(cfg_id, peers);
  UDPServer server(replica);

  std::thread guide_thread(guidance_service, std::ref(replica));
  server.init_service();

  server.start();
}