#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>

#include <unordered_map>

#include <loguru.hpp>

#include "morphling.h"
#include "transport.h"

class SocketTransport : public Transport {
  bufferevent *m_bev;
  void __send(MessageType type, uint8_t *payload, uint64_t payload_size);

 public:
  SocketTransport(bufferevent *bev) : m_bev(bev) {}
  ~SocketTransport() {}
  void send(uint8_t *buf, uint64_t size);
  void send(AppendEntriesMessage &msg);
  void send(AppenEntriesReplyMessage &msg);
  void send(ClientMessage &msg);
  void send(GuidanceMessage &msg);
};

void SocketTransport::send(uint8_t *buf, uint64_t size) {
  LOG_F(INFO, "[%s] send %zu bytes", __FUNCTION__, size);

  bufferevent_write(m_bev, buf, size);
}

void SocketTransport::__send(MessageType type, uint8_t *payload, uint64_t payload_size) {
  LOG_F(INFO, "[%s] send %zu bytes", __FUNCTION__, payload_size);

  char header_buf[4];
  for (int i = 0; i < 4; i++) {
    header_buf[i] = (payload_size >> (i * 8)) & 0xFF;
  }
  header_buf[3] = type & 0xFF;
  bufferevent_write(m_bev, header_buf, 4);
  bufferevent_write(m_bev, payload, payload_size);
}

void SocketTransport::send(AppendEntriesMessage &msg) {
  LOG_F(INFO, "send AppendEntriesMessage\n");
  std::stringstream stream;
  msgpack::pack(stream, msg);

  SocketTransport::__send(MsgTypeAppend, (uint8_t *)stream.str().data(), stream.str().size());
}
void SocketTransport::send(AppenEntriesReplyMessage &msg) {
  LOG_F(INFO, "send AppenEntriesReplyMessage\n");
  std::stringstream stream;
  msgpack::pack(stream, msg);

  SocketTransport::__send(MsgTypeAppendReply, (uint8_t *)stream.str().data(), stream.str().size());
}
void SocketTransport::send(ClientMessage &msg) {
  LOG_F(INFO, "send ClientMessage\n");
  std::stringstream stream;
  msgpack::pack(stream, msg);

  SocketTransport::__send(MsgTypeClient, (uint8_t *)stream.str().data(), stream.str().size());
}
void SocketTransport::send(GuidanceMessage &msg) {
  LOG_F(INFO, "send GuidanceMessage\n");
  std::stringstream stream;
  msgpack::pack(stream, msg);

  SocketTransport::__send(MsgTypeGuidance, (uint8_t *)stream.str().data(), stream.str().size());
}

class Server;
struct PeerContext {
  int id;
  struct event *retry_ev;
  Server *server;
  struct bufferevent *socket_bev;
  struct sockaddr_in peer_addr;
  bool proactive_connected = false;
  bool passive_connected = false;
};

class Server {
 public:
  std::unordered_map<int, std::string> m_peer_addresses;
  std::unordered_map<int, int> m_peer_port;
  std::unordered_map<int, std::unique_ptr<Transport>> m_peer_trans;
  std::unordered_map<int, PeerContext> m_peer_ctx;
  Morphling m_mpreplica;
  struct event_base *m_base;
  int m_me;
  std::vector<int> m_peers;

  Server(int id, std::vector<int> &peers)
      : m_mpreplica(id, peers), m_me(id), m_peers(peers) {
    struct event_base *base = event_base_new();
    m_base = base;
  }

  void run();
  void init_peer_ctx();
  bool connect_peer(int id);
  void periodic_peer(int id);
};

template <typename T>
void msg_unpck_msg(T &&target, uint8_t *buf, size_t size) {
  auto oh = msgpack::unpack((char *)buf, size);
  oh.get().convert(target);
}

void full_read(struct bufferevent *bev, uint8_t *buf, size_t size) {
  size_t remain_size = size;
  size_t turn_read_n = 0;
  size_t finish_read_n = 0;
  while (remain_size > 0) {
    turn_read_n = bufferevent_read(bev, buf + finish_read_n, remain_size);
    remain_size -= turn_read_n;
    finish_read_n += turn_read_n;
  }
}

void parse_and_feed_msg(struct bufferevent *bev, void *ctx) {
  Server *server = (Server *)ctx;
  // 2048 may be too small for large msg
  char tmp[2048] = {0};
  size_t rpc_header_size = 4;
  // size_t n = bufferevent_read(bev, tmp, rpc_header_size);
  // assert(n == rpc_header_size);
  full_read(bev, (uint8_t *)tmp, rpc_header_size);
  uint32_t payload_size = 0;
  for (int i = 0; i < rpc_header_size - 1; i++) {
    payload_size = payload_size | ((tmp[i] & 0x000000FF) << (i * 8));
    LOG_F(INFO, "tmp %d: 0x%x", i, tmp[i]);
  }
  MessageType msg_type = (MessageType)(tmp[3] & 0x000000FF);

  LOG_F(INFO, "payload size: %u", payload_size);
  LOG_F(INFO, "msg type: %d", msg_type);

  // n = bufferevent_read(bev, tmp, payload_size);
  // assert(n == payload_size);
  full_read(bev, (uint8_t *)tmp, payload_size);

  if (msg_type == MsgTypeAppend) {
    AppendEntriesMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
    server->m_mpreplica.handle_append_entries(msg, msg.from);
    server->m_mpreplica.bcast_msgs(server->m_peer_trans);
  } else if (msg_type == MsgTypeAppendReply) {
    AppenEntriesReplyMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
    server->m_mpreplica.handle_append_entries_reply(msg, msg.from);

  } else if (msg_type == MsgTypeClient) {
    ClientMessage msg;
    std::unique_ptr<Transport> trans = std::make_unique<SocketTransport>(bev);
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
    server->m_mpreplica.handle_operation(msg, std::move(trans));
    server->m_mpreplica.bcast_msgs(server->m_peer_trans);
  } else if (msg_type == MsgTypeGuidance) {
    GuidanceMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
  }
}

void read_cb(struct bufferevent *bev, void *ctx) {
  LOG_F(INFO, "read_cb");

  parse_and_feed_msg(bev, ctx);
}

void event_cb(struct bufferevent *bev, short what, void *ctx) {
  int needfree = 0;

  LOG_F(INFO, "[%s:%d] event_cb new event 0x%x", __FUNCTION__, __LINE__, what);

  if (what & BEV_EVENT_EOF || what & BEV_EVENT_TIMEOUT ||
      what & BEV_EVENT_ERROR) {
    needfree = 1;
  }

  if (needfree) {
    int errcode = evutil_socket_geterror(bufferevent_getfd(bev));
    LOG_F(INFO, "errcode: %d", errcode);
    bufferevent_free(bev);
  }
}

void listener_cb(struct evconnlistener *l, evutil_socket_t nfd,
                 struct sockaddr *addr, int socklen, void *ctx) {
  Server *server = (Server *)ctx;
  // struct event_base* base = (struct event_base*)ctx;

  struct bufferevent *bev =
      bufferevent_socket_new(server->m_base, nfd, BEV_OPT_CLOSE_ON_FREE);
  assert(bev);
  LOG_F(INFO, "new bufferevent at %p", bev);


  char *ip = inet_ntoa(((struct sockaddr_in *)addr)->sin_addr);
  uint16_t port = ntohs(((struct sockaddr_in *)addr)->sin_port);
  LOG_F(INFO, "accept new connection: %s:%u", ip, port);

  bufferevent_setcb(bev, read_cb, nullptr, event_cb, ctx);
  bufferevent_enable(bev, EV_READ | EV_PERSIST);
}

void signal_cb(evutil_socket_t sig, short events, void *arg) {
  Server *server = (Server *)arg;
  struct timeval delay = {2, 0};

  LOG_F(INFO, "Caught an interrupt signal; exiting cleanly in two seconds.");

  for (auto item : server->m_peer_ctx) {
    if (item.first != server->m_me) {
      event_del(item.second.retry_ev);
    }
  }
  event_base_loopexit(server->m_base, &delay);
}

void Server::run() {
  Server::init_peer_ctx();

  struct sockaddr saddr;
  int socklen = sizeof(saddr);

  std::string local_service = "0.0.0.0:" + std::to_string(m_peer_port[m_me]);

  int ret = evutil_parse_sockaddr_port(local_service.c_str(), &saddr, &socklen);
  assert(ret == 0);

  struct evconnlistener *lev = evconnlistener_new_bind(
      m_base, listener_cb, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 100,
      &saddr, socklen);
  assert(lev);

  struct event *ev_signal = evsignal_new(m_base, SIGINT, signal_cb, this);
  event_add(ev_signal, NULL);

  for (auto pid : m_peers) {
    if (pid != m_me) {
      periodic_peer(pid);
    }
  }

  event_base_dispatch(m_base);

  evconnlistener_free(lev);

  event_base_free(m_base);
}

void Server::init_peer_ctx() {
  for (auto pid : m_peers) {
    if (pid != m_me) {
      m_peer_ctx[pid] = PeerContext{
          .id = pid,
          .server = this,
      };
      auto &peer = m_peer_ctx[pid];
      bufferevent *bev =
          bufferevent_socket_new(m_base, -1, BEV_OPT_CLOSE_ON_FREE);
      memset(&peer.peer_addr, 0, sizeof(struct sockaddr_in));
      peer.peer_addr.sin_family = AF_INET;
      peer.peer_addr.sin_port = htons(m_peer_port[pid]);
      inet_aton(m_peer_addresses[pid].c_str(), &peer.peer_addr.sin_addr);
      peer.socket_bev = bev;
    }
  }
}

bool Server::connect_peer(int id) {
  auto &peer = m_peer_ctx[id];
  int res = bufferevent_socket_connect(peer.socket_bev,
                                       (struct sockaddr *)&peer.peer_addr,
                                       sizeof(struct sockaddr_in));
  if (res != 0) {
    LOG_F(INFO, "bufferevent_socket_connect failure!");
    return false;
  }
  bufferevent_setcb(
      peer.socket_bev, nullptr, nullptr,
      [](struct bufferevent *bev, short what, void *arg) {
        PeerContext *ctx = (PeerContext *)arg;
        LOG_F(INFO, "get event: 0x%x, from peer %d", what, ctx->id);
        if (what & BEV_EVENT_CONNECTED) {
          ctx->proactive_connected = true;
        }
      }, &peer);
  bufferevent_enable(peer.socket_bev, EV_READ | EV_WRITE | EV_ET | EV_PERSIST);
  m_peer_trans[id] = std::make_unique<SocketTransport>(peer.socket_bev);
  return true;
}

void Server::periodic_peer(int id) {
  struct event *ev_period = event_new(
      m_base, -1, EV_PERSIST,
      [](evutil_socket_t fd, short what, void *arg) {
        PeerContext *ctx = (PeerContext *)arg;
        if (!ctx->proactive_connected && !ctx->passive_connected) {
          LOG_F(INFO, "to %d, what = 0x%x", ctx->id, what);
          ctx->server->connect_peer(ctx->id);
        }
      },
      &m_peer_ctx[id]);
  struct timeval one_sec = {1, 0};
  m_peer_ctx[id].retry_ev = ev_period;
  event_add(ev_period, &one_sec);
}

int id = 0;
std::vector<std::string> peers_addr;

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
            id = std::stoi(optarg);
            break;

          case 1:
            LOG_F(1, "peers: %s", optarg);
            mandatory += 1;
            std::string arg(optarg);
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
    LOG_F(ERROR, "needs args: --peers-addr, a comma separated string, like <ip1>,<ip2>,<ip3>");
    LOG_F(ERROR, "current peers' ports are hard coded: 9990, 9991 and 9992");
    return false;
  }

  return true;
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }

  std::vector<int> peers{0, 1, 2};
  Server server(id, peers);

  server.m_peer_addresses[0] = peers_addr[0];
  server.m_peer_addresses[1] = peers_addr[1];
  server.m_peer_addresses[2] = peers_addr[2];

  server.m_peer_port[0] = 9990;
  server.m_peer_port[1] = 9991;
  server.m_peer_port[2] = 9992;

  server.run();
}