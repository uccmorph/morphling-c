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
#include <string.h>
#include <netinet/tcp.h>

#include <chrono>
#include <loguru.hpp>
#include <unordered_map>

#include "morphling.h"
#include "transport.h"
#include "utils.h"

Gauge g_gauge("Client op");
Gauge g_append_gauge("Append");
Gauge g_handle_append_gauge("New entry");
Gauge g_send_gauge("send");

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
  void send(ClientReplyMessage &msg);
  void send(GuidanceMessage &msg);
};

void SocketTransport::send(uint8_t *buf, uint64_t size) {
  LOG_F(3, "[%s] send %zu bytes", __FUNCTION__, size);

  bufferevent_write(m_bev, buf, size);
}

void SocketTransport::__send(MessageType type, uint8_t *payload,
                             uint64_t payload_size) {
  LOG_F(5, "[%s] send %zu bytes", __FUNCTION__, payload_size);
  g_send_gauge.set_probe1();
  char header_buf[4];
  for (int i = 0; i < 4; i++) {
    header_buf[i] = (payload_size >> (i * 8)) & 0xFF;
  }
  header_buf[3] = type & 0xFF;
  bufferevent_write(m_bev, header_buf, 4);
  bufferevent_write(m_bev, payload, payload_size);
  auto probe_idx = g_send_gauge.set_probe2();
  g_send_gauge.instant_time_us(probe_idx);
}

void SocketTransport::send(AppendEntriesMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  g_append_gauge.set_probe1();
  SocketTransport::__send(MsgTypeAppend, (uint8_t *)stream.str().data(),
                          stream.str().size());
}
void SocketTransport::send(AppenEntriesReplyMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);


  SocketTransport::__send(MsgTypeAppendReply, (uint8_t *)stream.str().data(),
                          stream.str().size());
  auto probe_idx = g_handle_append_gauge.set_probe2();
  g_handle_append_gauge.instant_time_us(probe_idx);
}
void SocketTransport::send(ClientMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  SocketTransport::__send(MsgTypeClient, (uint8_t *)stream.str().data(),
                          stream.str().size());
}
void SocketTransport::send(ClientReplyMessage &msg) {
  std::stringstream stream;
  msgpack::pack(stream, msg);

  auto probe_idx = g_gauge.set_probe2();
  g_gauge.instant_time_us(probe_idx);

  SocketTransport::__send(MsgTypeClientReply, (uint8_t *)stream.str().data(),
                          stream.str().size());
}
void SocketTransport::send(GuidanceMessage &msg) {
  // std::stringstream stream;
  // msgpack::pack(stream, msg);
  uint8_t *payload = (uint8_t *)&msg;
  uint64_t size = sizeof(msg);

  SocketTransport::__send(MsgTypeGuidance, payload, size);
}

class Server;
struct PeerContext {
  int id;
  struct event *retry_ev;
  Server *server;
  struct bufferevent *socket_bev;

  bool proactive_connected = false;
  bool passive_connected = false;
};

class Server {
 public:
  std::unordered_map<int, std::string> m_peer_addresses;
  std::unordered_map<int, int> m_peer_port;
  std::unordered_map<int, int> m_service_port;
  std::unordered_map<int, std::unique_ptr<Transport>> m_peer_trans;
  std::unordered_map<int, PeerContext> m_peer_ctx;
  Morphling replica;
  struct event_base *m_base;
  int m_me;
  std::vector<int> m_peers;
  std::vector<std::chrono::steady_clock::time_point> m_measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> m_measure_probe2;
  std::vector<std::chrono::steady_clock::time_point> m_measure_probe3;

  Server(int id, std::vector<int> &peers)
      : replica(id, peers), m_me(id), m_peers(peers) {
    struct event_base *base = event_base_new();
    m_base = base;
    m_measure_probe1.reserve(100000);
    m_measure_probe2.reserve(100000);
    m_measure_probe3.reserve(100000);
  }

  void run();
  void init_peer_ctx();
  bool connect_peer(int id);
  void periodic_peer(int id);

  evconnlistener *start_service();
  evconnlistener *start_peer();
  void recv_init_msg(bufferevent *bev);
};

template <typename T>
void msg_unpck_msg(T &&target, uint8_t *buf, size_t size) {
  auto oh = msgpack::unpack((char *)buf, size);
  oh.get().convert(target);
}

void client_msg_cb(struct bufferevent *bev, void *ctx) {
  Server *server = (Server *)ctx;
  uint8_t tmp[2048];
  size_t msg_size = 0;
  auto type = recv_msg(bev, tmp, msg_size);

  if (type == MsgTypeClient) {
    g_gauge.set_probe1();
    ClientMessage msg;
    std::unique_ptr<Transport> trans = std::make_unique<SocketTransport>(bev);
    auto oh = msgpack::unpack((char *)tmp, msg_size);
    oh.get().convert(msg);
    server->replica.handle_operation(msg, std::move(trans));
    server->replica.bcast_msgs(server->m_peer_trans);
  } else if (type == MsgTypeGetGuidance) {
    server->replica.reply_guidance(std::make_unique<SocketTransport>(bev));
  } else {
    LOG_F(ERROR, "type %d is not client message", type);
    return;
  }
}

void peer_init_msg_cb(struct bufferevent *bev, void *ctx) {
  Server *server = (Server *)ctx;
  server->recv_init_msg(bev);
}

void peer_general_msg_cb(struct bufferevent *bev, void *arg) {
  PeerContext *peer = (PeerContext *)arg;
  Server *server = peer->server;
  // LOG_F(3, "new message fomr peer %d", peer->id);
  uint8_t tmp[2048];
  size_t msg_size = 0;
  auto type = recv_msg(bev, tmp, msg_size);
  if (type == MsgTypeUnknown) {
    LOG_F(3, "from peer %d, reply with 404-like message", peer->id);
    std::unique_ptr<Transport> trans = std::make_unique<SocketTransport>(bev);
    std::stringstream error_msg;
    error_msg << "error: don't have handler for " << type << "\n";
    drain_read(bev, tmp, 2048);
    trans->send((uint8_t *)error_msg.str().c_str(), error_msg.str().size());
    return;
  }

  if (type == MsgTypeAppend) {
    LOG_F(3, "new MsgTypeAppend fomr peer %d", peer->id);
    g_handle_append_gauge.set_probe1();
    AppendEntriesMessage msg;
    auto oh = msgpack::unpack((char *)tmp, msg_size);
    oh.get().convert(msg);
    server->replica.handle_append_entries(msg, msg.from);

  } else if (type == MsgTypeAppendReply) {
    LOG_F(3, "new MsgTypeAppendReply fomr peer %d", peer->id);
    auto probe_idx = g_append_gauge.set_probe2();
    g_append_gauge.instant_time_us(probe_idx);
    AppenEntriesReplyMessage msg;
    auto oh = msgpack::unpack((char *)tmp, msg_size);
    oh.get().convert(msg);
    server->replica.handle_append_entries_reply(msg, msg.from);
    server->replica.maybe_apply();

  } else if (type == MsgTypeGuidance) {
    GuidanceMessage *msg = (GuidanceMessage *)tmp;
    // LOG_F(INFO, "receive guidance msg from %d", msg->from);
    // debug_print_guidance(&msg->guide);
  } else {
    LOG_F(ERROR, "type %d is not peer message", type);
    return;
  }
  server->replica.bcast_msgs(server->m_peer_trans);
}

void event_cb(struct bufferevent *bev, short what, void *ctx) {
  int needfree = 0;

  LOG_F(3, "[%s:%d] event_cb new event 0x%x", __FUNCTION__, __LINE__, what);

  if (what & BEV_EVENT_EOF || what & BEV_EVENT_TIMEOUT ||
      what & BEV_EVENT_ERROR) {
    needfree = 1;
  }

  if (needfree) {
    int errcode = evutil_socket_geterror(bufferevent_getfd(bev));
    LOG_F(INFO, "errcode: %d, %s", errcode, std::strerror(errcode));
    bufferevent_free(bev);
  }
}

void connect_peer_event_cb(struct bufferevent *bev, short what, void *arg) {
  PeerContext *ctx = (PeerContext *)arg;
  LOG_F(3, "get event: 0x%x, from peer %d", what, ctx->id);
  if (what & BEV_EVENT_CONNECTED) {
    if (ctx->passive_connected) {
      LOG_F(INFO, "peer %d has passive connect", ctx->id);
    } else {
      ctx->proactive_connected = true;
      LOG_F(INFO, "send GuidanceMessage on proactive connection");
      ctx->server->replica.reply_guidance(
          std::make_unique<SocketTransport>(bev));
      ctx->server->m_peer_trans[ctx->id] =
          std::make_unique<SocketTransport>(bev);
    }
  }
}

void accept_client(struct evconnlistener *l, evutil_socket_t nfd,
                   struct sockaddr *addr, int socklen, void *ctx) {
  Server *server = (Server *)ctx;

  struct bufferevent *bev =
      bufferevent_socket_new(server->m_base, nfd, BEV_OPT_CLOSE_ON_FREE);
  assert(bev);

  char *ip = inet_ntoa(((struct sockaddr_in *)addr)->sin_addr);
  uint16_t port = ntohs(((struct sockaddr_in *)addr)->sin_port);
  LOG_F(3, "accept new connection: %s:%u", ip, port);

  bufferevent_setcb(bev, client_msg_cb, nullptr, event_cb, ctx);
  bufferevent_enable(bev, EV_READ | EV_PERSIST | EV_WRITE);
}

void accept_peer(struct evconnlistener *l, evutil_socket_t nfd,
                 struct sockaddr *addr, int socklen, void *ctx) {
  Server *server = (Server *)ctx;

  struct bufferevent *bev =
      bufferevent_socket_new(server->m_base, nfd, BEV_OPT_CLOSE_ON_FREE);
  assert(bev);

  char *ip = inet_ntoa(((struct sockaddr_in *)addr)->sin_addr);
  uint16_t port = ntohs(((struct sockaddr_in *)addr)->sin_port);
  LOG_F(3, "accept new peer: %s:%u", ip, port);

  bufferevent_setcb(bev, peer_init_msg_cb, nullptr, event_cb, ctx);
  bufferevent_enable(bev, EV_READ | EV_ET);
}

void signal_cb(evutil_socket_t sig, short events, void *arg) {
  Server *server = (Server *)arg;

  LOG_F(INFO, "Caught an interrupt signal; exiting cleanly.");

  for (auto item : server->m_peer_ctx) {
    if (item.first != server->m_me) {
      event_del(item.second.retry_ev);
    }
  }
  event_base_loopexit(server->m_base, nullptr);
}

evconnlistener *start_listener(event_base *base, int port, evconnlistener_cb cb,
                               void *arg) {
  struct sockaddr saddr;
  int socklen = sizeof(saddr);

  std::string local_service = "0.0.0.0:" + std::to_string(port);

  int ret = evutil_parse_sockaddr_port(local_service.c_str(), &saddr, &socklen);
  assert(ret == 0);

  evconnlistener *lev = evconnlistener_new_bind(
      base, cb, arg, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, &saddr,
      socklen);
  assert(lev);

  return lev;
}

evconnlistener *Server::start_service() {
  return start_listener(m_base, m_service_port[m_me], accept_client, this);
}

evconnlistener *Server::start_peer() {
  return start_listener(m_base, m_peer_port[m_me], accept_peer, this);
}

void Server::recv_init_msg(bufferevent *bev) {
  uint8_t tmp[2048];
  size_t msg_size = 0;
  auto type = recv_msg(bev, tmp, msg_size);
  if (type != MsgTypeGuidance) {
    LOG_F(INFO, "first peer msg is not GuidanceMsg");
    bufferevent_setcb(bev, peer_init_msg_cb, nullptr, event_cb, this);
    bufferevent_enable(bev, EV_READ | EV_ET);
    return;
  }
  GuidanceMessage *msg = (GuidanceMessage *)tmp;
  LOG_F(INFO, "receive guidance msg from %d", msg->from);
  debug_print_guidance(&msg->guide);
  LOG_F(INFO, "peer ctx old socket_bev: %p, maybe need change",
        m_peer_ctx[msg->from].socket_bev);

  if (m_peer_ctx[msg->from].proactive_connected) {
    LOG_F(INFO, "peer %d has proactive connect, give up this connection", msg->from);
    bufferevent_free(bev);
  } else {
    m_peer_ctx[msg->from].passive_connected = true;
    if (m_peer_ctx[msg->from].socket_bev != nullptr) {
      bufferevent_free(m_peer_ctx[msg->from].socket_bev);
    }
    m_peer_ctx[msg->from].socket_bev = bev;
    m_peer_trans[msg->from] = std::make_unique<SocketTransport>(bev);
    int fd = bufferevent_getfd(bev);
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
    bufferevent_setcb(bev, peer_general_msg_cb, nullptr, event_cb, &m_peer_ctx[msg->from]);
    bufferevent_enable(bev, EV_READ | EV_WRITE | EV_PERSIST | EV_ET);
  }
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

      peer.socket_bev = bev;
    }
  }
}

bool Server::connect_peer(int id) {
  auto &peer = m_peer_ctx[id];
  struct sockaddr_in peer_addr;
  memset(&peer_addr, 0, sizeof(struct sockaddr_in));
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_port = htons(m_peer_port[id]);
  inet_aton(m_peer_addresses[id].c_str(), &peer_addr.sin_addr);
  int res =
      bufferevent_socket_connect(peer.socket_bev, (struct sockaddr *)&peer_addr,
                                 sizeof(struct sockaddr_in));

  int fd = bufferevent_getfd(peer.socket_bev);
  int flag = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
  if (res != 0) {
    LOG_F(INFO, "bufferevent_socket_connect failure! code: %d", errno);
    return false;
  }
  bufferevent_setcb(peer.socket_bev, peer_general_msg_cb, nullptr,
                    connect_peer_event_cb, &peer);
  bufferevent_enable(peer.socket_bev, EV_READ | EV_WRITE | EV_PERSIST | EV_ET);

  return true;
}

void Server::periodic_peer(int id) {
  struct event *ev_period = event_new(
      m_base, -1, EV_PERSIST,
      [](evutil_socket_t fd, short what, void *arg) {
        PeerContext *peer = (PeerContext *)arg;
        if (!peer->proactive_connected && !peer->passive_connected) {
          LOG_F(INFO, "peer %d not connected, event = 0x%x", peer->id, what);
        } else {
          peer->server->replica.reply_guidance(std::make_unique<SocketTransport>(peer->socket_bev));
        }
      },
      &m_peer_ctx[id]);
  struct timeval one_sec = {5, 0};
  m_peer_ctx[id].retry_ev = ev_period;
  event_add(ev_period, &one_sec);
}

void Server::run() {
  Server::init_peer_ctx();

  evconnlistener *ls = Server::start_service();
  evconnlistener *lp = Server::start_peer();

  struct event *ev_signal = evsignal_new(m_base, SIGINT, signal_cb, this);
  event_add(ev_signal, NULL);

  for (auto pid : m_peers) {
    if (pid != m_me) {
      connect_peer(pid);
      periodic_peer(pid);
    }
  }

  event_base_dispatch(m_base);

  LOG_F(WARNING, "event loop finish");
  evconnlistener_free(ls);
  evconnlistener_free(lp);
  event_base_free(m_base);
}

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

  return true;
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);

  std::vector<int> peers{0, 1, 2};
  Server server(cfg_id, peers);

  server.m_peer_addresses[0] = cfg_peers_addr[0];
  server.m_peer_addresses[1] = cfg_peers_addr[1];
  server.m_peer_addresses[2] = cfg_peers_addr[2];

  server.m_service_port[0] = 9990;
  server.m_service_port[1] = 9991;
  server.m_service_port[2] = 9992;

  server.m_peer_port[0] = 9993;
  server.m_peer_port[1] = 9994;
  server.m_peer_port[2] = 9995;

  server.run();
}