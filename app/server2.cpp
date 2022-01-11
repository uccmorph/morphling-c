#include <signal.h>
#include <getopt.h>


#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

#include <chrono>
#include <loguru.hpp>
#include <unordered_map>

#include "morphling.h"
#include "transport.h"
#include "utils.h"

int cfg_id = 0;
std::vector<std::string> cfg_peers_addr;

bool parse_cmd(int argc, char **argv) {
  static struct option long_options[] = {
      {"id", required_argument, nullptr, 0},
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

class UDPTransport: public Transport {
  int m_fd;
  sockaddr_in m_addr;

  void __send(MessageType type, uint8_t *payload, uint64_t payload_size);

 public:
  UDPTransport(int fd, sockaddr_in addr) : m_fd(fd), m_addr(addr) {}
  ~UDPTransport() {}
  void send(uint8_t *buf, uint64_t size);
  void send(AppendEntriesMessage &msg);
  void send(AppenEntriesReplyMessage &msg);
  void send(ClientMessage &msg);
  void send(ClientReplyMessage &msg);
  void send(GuidanceMessage &msg);
};

void UDPTransport::__send(MessageType type, uint8_t *payload, uint64_t payload_size) {
  MessageHeader header;
  header.type = type;
  header.size = payload_size;

  size_t size = sizeof(header) + payload_size;
  uint8_t *buf = new uint8_t[size];
  memcpy(buf, &header, sizeof(header));
  memcpy(buf + sizeof(header), payload, payload_size);

  int send_n = sendto(m_fd, buf, size, 0, (sockaddr *)&m_addr, sizeof(sockaddr_in));
  LOG_F(INFO, "send_n = %d, size = %zu", send_n, size);
  if (send_n == -1) {
    LOG_F(FATAL, "udp send error: %d, %s", errno, std::strerror(errno));
  }
  assert(send_n == (int)size);

  delete []buf;
}

void UDPTransport::send(uint8_t *buf, uint64_t size) {}

void UDPTransport::send(AppendEntriesMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  __send(MessageType::MsgTypeAppend, (uint8_t *)stream.str().c_str(), stream.str().size());
}

void UDPTransport::send(AppenEntriesReplyMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  __send(MessageType::MsgTypeAppendReply, (uint8_t *)stream.str().c_str(), stream.str().size());
}

void UDPTransport::send(ClientMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  __send(MessageType::MsgTypeClient, (uint8_t *)stream.str().c_str(), stream.str().size());
}

void UDPTransport::send(ClientReplyMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  __send(MessageType::MsgTypeClientReply, (uint8_t *)stream.str().c_str(), stream.str().size());
}

void UDPTransport::send(GuidanceMessage &msg) {
  uint8_t *payload = (uint8_t *)&msg;
  uint64_t size = sizeof(msg);
  LOG_F(3, "send GuidanceMessage from %d, votes: %d", msg.from, msg.votes);
  __send(MessageType::MsgTypeGuidance, payload, size);
}

class UDPServer {
public:
  struct PeerInfo {
    std::string ip;
    int peer_port;
    int service_port;

    sockaddr_in sock_addr;
    int peer_fd;
  };

private:
  event_base *m_base;
  int m_service_socket;
  int m_peer_socket;
  std::vector<PeerInfo> m_peer_info;
  int m_me;
  std::unordered_map<int, std::unique_ptr<Transport>> m_peer_trans;

public:
  Morphling *replica;

public:
  UDPServer();
  void set_peer_info(int self, std::vector<std::string> peer_ip,
                     std::vector<int> &service_port,
                     std::vector<int> &peer_port);
  bool init_udp(int fd, int port);
  void init_service();
  void start();
  void on_udp_event();

  std::vector<PeerInfo>& get_peer_info();
  int get_id();
};

UDPServer::UDPServer() {
  m_base = event_base_new();
  m_peer_socket = socket(AF_INET, SOCK_DGRAM, 0);
  m_service_socket = socket(AF_INET, SOCK_DGRAM, 0);
}

void UDPServer::set_peer_info(int self, std::vector<std::string> peer_ip,
                   std::vector<int> &service_port,
                   std::vector<int> &peer_port) {
  m_peer_info.resize(peer_ip.size());
  m_me = self;
  for (size_t id = 0; id < peer_ip.size(); id++) {
    m_peer_info[id].ip = peer_ip[id];
    m_peer_info[id].service_port = service_port[id];
    m_peer_info[id].peer_port = peer_port[id];


    if ((int)id != m_me) {
      sockaddr_in &addr = m_peer_info[id].sock_addr;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = inet_addr(peer_ip[id].c_str());
      addr.sin_port = htons(peer_port[id]);
      m_peer_info[id].peer_fd = socket(AF_INET, SOCK_DGRAM, 0);
      m_peer_trans[id] = std::make_unique<UDPTransport>(m_peer_info[id].peer_fd, addr);
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

void UDPServer::init_service() {

  init_udp(m_peer_socket, m_peer_info[m_me].peer_port);
  init_udp(m_service_socket, m_peer_info[m_me].service_port);

  int flag = EV_READ | EV_PERSIST;
  event *ev_service = event_new(
      m_base, m_service_socket, flag, [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        LOG_F(INFO, "new client event");
        if (what & EV_READ) {
          sockaddr_in addr;
          socklen_t addr_len = sizeof(addr);
          MessageHeader header;
          uint8_t *buf = reinterpret_cast<uint8_t *>(&header);
          int read_n = recvfrom(fd, buf, sizeof(header), MSG_PEEK, (sockaddr *)&addr, &addr_len);
          assert(read_n == (int)sizeof(header));

          if (header.type == MessageType::MsgTypeClient) {
            size_t msg_size = sizeof(header) + header.size;
            // uint8_t *msg_buf = new uint8_t[msg_size];
            uint8_t msg_buf[2048];
            int read_n = recvfrom(fd, msg_buf, msg_size, 0, (sockaddr *)&addr, &addr_len);
            assert(read_n == (int)msg_size);
            uint8_t *tmp = msg_buf + sizeof(header);

            ClientMessage msg;
            auto oh = msgpack::unpack((char *)tmp, header.size);
            oh.get().convert(msg);
            LOG_F(INFO, "receive MsgTypeClient, key: %lx", msg.key_hash);
            std::unique_ptr<Transport> trans = std::make_unique<UDPTransport>(fd, addr);

            server->replica->handle_operation(msg, std::move(trans));
            server->replica->bcast_msgs(server->m_peer_trans);

            // delete []msg_buf;
          } else if (header.type == MessageType::MsgTypeGetGuidance) {
            uint8_t msg_buf[4];
            recvfrom(fd, msg_buf, 0, 0, (sockaddr *)&addr, &addr_len);
            server->replica->reply_guidance(std::make_unique<UDPTransport>(fd, addr));
          } else {
            LOG_F(ERROR, "client msg error type %d", header.type);
          }
        }
      },
      this);
  event_add(ev_service, nullptr);

  event *ev_peer = event_new(
      m_base, m_peer_socket, flag, [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        if (what & EV_READ) {
          sockaddr_in addr;
          socklen_t addr_len = sizeof(addr);
          MessageHeader header;
          uint8_t *buf = reinterpret_cast<uint8_t *>(&header);
          int read_n = recvfrom(fd, buf, sizeof(header), MSG_PEEK, (sockaddr *)&addr, &addr_len);
          assert(read_n == (int)sizeof(header));

          size_t msg_size = sizeof(header) + header.size;
          uint8_t *msg_buf = new uint8_t[msg_size];
          read_n = recvfrom(fd, msg_buf, msg_size, 0, (sockaddr *)&addr, &addr_len);
          assert(read_n == (int)msg_size);
          uint8_t *tmp = msg_buf + sizeof(header);

          if (header.type == MessageType::MsgTypeAppend) {
            AppendEntriesMessage msg;
            auto oh = msgpack::unpack((char *)tmp, header.size);
            oh.get().convert(msg);

            server->replica->handle_append_entries(msg, msg.from);
          } else if (header.type == MessageType::MsgTypeAppendReply) {
            AppenEntriesReplyMessage msg;
            auto oh = msgpack::unpack((char *)tmp, header.size);
            oh.get().convert(msg);

            server->replica->handle_append_entries_reply(msg, msg.from);
            server->replica->maybe_apply();
          } else if (header.type == MessageType::MsgTypeGuidance) {
            GuidanceMessage *msg = (GuidanceMessage *)tmp;
            LOG_F(INFO, "receive guidance msg from %d", msg->from);
            // debug_print_guidance(&msg->guide);
          } else {
            LOG_F(ERROR, "peer msg error type %d", header.type);
          }
          server->replica->bcast_msgs(server->m_peer_trans);

          delete []msg_buf;
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
          server->replica->reply_guidance(std::make_unique<UDPTransport>(
              peer_info[id].peer_fd, peer_info[id].sock_addr));
        }
      },
      this);
  struct timeval one_sec = {5, 0};
  event_add(ev_period, &one_sec);
}

void UDPServer::start() {
  event_base_dispatch(m_base);
}

std::vector<UDPServer::PeerInfo>& UDPServer::get_peer_info() {
  return m_peer_info;
}

int UDPServer::get_id() {
  return m_me;
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);

  std::vector<int> peers{0, 1, 2};
  std::vector<int> service_port(3);
  std::vector<int> peer_port(3);
  service_port[0] = 9990;
  service_port[1] = 9991;
  service_port[2] = 9992;

  peer_port[0] = 9993;
  peer_port[1] = 9994;
  peer_port[2] = 9995;

  Morphling replica(cfg_id, peers);
  UDPServer server;

  server.replica = &replica;
  server.set_peer_info(cfg_id, cfg_peers_addr, service_port, peer_port);

  server.init_service();

  server.start();

}