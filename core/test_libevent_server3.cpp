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

#include <string>
#include <functional>
#include <unordered_map>

void new_udp_event(evutil_socket_t fd, short what, void *arg);

struct MessageHeader {
  uint32_t size:24;
  uint32_t type:8;

  static MessageHeader* deserialize(uint8_t *buf, size_t size) {
    if (size != sizeof(MessageHeader)) {
      return nullptr;
    }
    return reinterpret_cast<MessageHeader *>(buf);
  }
  std::pair<uint8_t *, size_t> cast();
};

std::pair<uint8_t *, size_t> MessageHeader::cast() {
  return {reinterpret_cast<uint8_t *>(this), sizeof(*this)};
}

struct Message {
  MessageHeader header;
};

struct ClientMessage: public Message {
  int client_id;
};

struct NestedMessage: public Message {
  int data;
};

struct NestedReplyMessage: public Message {
  int data;
};

struct ClientReplyMessage: public Message {
  int data;
};

class UDPServer {
  int m_sock_fd;
  event_base *m_base;
  std::unordered_map<int, std::function<void (Message &)>> m_callbacks;
  int m_me;
  std::unordered_map<int, std::string> m_peer_addr;
  std::unordered_map<int, int> m_peer_port;

private:
  bool init_udp(int port);

public:
  UDPServer();
  bool init_base_service();
  void set_info(int self, std::unordered_map<int, std::string> &peer_addr,
                std::unordered_map<int, int> &peer_port);

  void start();
  void handle_msg(int fd);

  void register_cb(int type, std::function<void ()> cb);
  void register_cb(int type, std::function<void (Message &)> cb);
};

bool UDPServer::init_udp(int port) {
  struct sockaddr_in sin;
  int socklen = sizeof(sin);
  memset(&sin, 0, socklen);

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(port);

  if (bind(m_sock_fd, (sockaddr *)&sin, sizeof(sin)) == -1) {
    printf("bind error: %d, %s\n", errno, strerror(errno));
    return false;
  }

  return true;
}

UDPServer::UDPServer() {
  m_base = event_base_new();
  m_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

  printf("base sock fd: %d\n", m_sock_fd);
}

bool UDPServer::init_base_service() {

  auto port = m_peer_port[m_me];
  if (!init_udp(port)) {
    return false;
  }

  int ev_flag = EV_READ | EV_PERSIST;
  event *l_ev = event_new(m_base, m_sock_fd, ev_flag, new_udp_event, this);
  event_add(l_ev, nullptr);

  return true;
}

void UDPServer::set_info(int self,
                         std::unordered_map<int, std::string> &peer_addr,
                         std::unordered_map<int, int> &peer_port) {
  m_me = self;
  m_peer_addr = peer_addr;
  m_peer_port = peer_port;
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
  auto [buf, size] = header.cast();
  int read_n = recvfrom(fd, buf, size, MSG_PEEK, (sockaddr *)&addr, &addr_len);
  printf("read %d bytes from %s:%d\n", read_n, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
  printf("header, size: %u, type: %u\n", header.size, header.type);

  auto find_itr = m_callbacks.find((int)header.type);
  if (find_itr == m_callbacks.end()) {
    printf("type %u is not register\n", header.type);
    uint8_t drain_buf[8];

    sleep(1);
    read_n = recvfrom(fd, drain_buf, 0, 0, (sockaddr *)&addr, &addr_len);
    printf("drain %d bytes, want %zu\n", read_n, sizeof(MessageHeader));

    auto reply = MessageHeader::deserialize(drain_buf, sizeof(MessageHeader));
    printf("reply size: %u, type: %u\n", reply->size, reply->type);
    return;
  }

  sendto(fd, buf, read_n, 0, (sockaddr *)&addr, addr_len);
  printf("send back\n");
}

void new_udp_event(evutil_socket_t fd, short what, void *arg) {
  UDPServer *server = reinterpret_cast<UDPServer *>(arg);

  printf("fd: %d new event: 0x%x\n", fd, what);
  if (what & EV_READ) {
    server->handle_msg(fd);
  } else {
    printf("unhandled event\n");
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

#if 0
void udp_echo_loop(int port) {
  struct sockaddr_in sin;
  int socklen = sizeof(sin);
  memset(&sin, 0, socklen);

  int m_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  printf("base sock fd: %d\n", m_sock_fd);
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(port);

  if (bind(m_sock_fd, (sockaddr *)&sin, sizeof(sin)) == -1) {
    printf("bind error: %d, %s\n", errno, strerror(errno));
    return;
  }

  while (true) {
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    // if we don't read it out, then it may keep notifying EV_READ
    // but, after setting EV_ET, then it doesn't matter if we don't read

    uint8_t *buf[2048];
    int read_n = recvfrom(m_sock_fd, buf, 2048, 0, (sockaddr *)&addr, &addr_len);
    printf("read %d bytes from %s:%d\n", read_n, inet_ntoa(addr.sin_addr),
           ntohs(addr.sin_port));
    printf("addr_len = %d\n", addr_len);

    sendto(m_sock_fd, buf, read_n, 0, (sockaddr *)&addr, addr_len);
    printf("send back\n");
  }
}
#endif

int main() {

  std::unordered_map<int, std::string> peer_addr;
  peer_addr[0] = "127.0.0.1";
  peer_addr[1] = "127.0.0.1";
  peer_addr[2] = "127.0.0.1";

  std::unordered_map<int, int> peer_port;
  peer_port[0] = 40713;
  peer_port[1] = 40714;
  peer_port[2] = 40715;

  UDPServer server;
  server.set_info(0, peer_addr, peer_port);
  server.init_base_service();

  server.start();

  // udp_echo_loop(40713);
}