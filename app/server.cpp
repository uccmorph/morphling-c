#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>

#include <unordered_map>

#include "morphling.h"
#include "transport.h"

class SocketTransport : public Transport {
  bufferevent *m_bev;

 public:
  SocketTransport(bufferevent *bev) : m_bev(bev) {}
  ~SocketTransport() {}
  void send(uint8_t *buf, uint64_t size);
};

void SocketTransport::send(uint8_t *buf, uint64_t size) {
  bufferevent_write(m_bev, buf, size);
}

struct PeerContext {
  int id;
  struct event *m_retry_ev;
};

class Server {
 public:
  std::unordered_map<int, std::string> m_peer_addresses;
  std::unordered_map<int, int> m_peer_port;
  std::unordered_map<int, std::unique_ptr<Transport>> m_peer_trans;
  std::unordered_map<int, PeerContext> m_peer_retry;
  Morphling m_mpreplica;
  struct event_base *m_base;
  int m_me;
  std::vector<int> m_peers;

  Server(int id, std::vector<int> &peers) : m_mpreplica(id, peers) {
    m_me = id;
    m_peers = peers;
    for (auto pid : peers) {
      m_peer_retry[pid] = PeerContext{
          .id = pid,
      };
    }
  }
  void start();
  void connect_peer(int id);
  void periodic_peer(int id);
};

template <typename T>
void msg_unpck_msg(T &&target, uint8_t *buf, size_t size) {
  auto oh = msgpack::unpack((char *)buf, size);
  oh.get().convert(target);
}

void parse_and_feed_msg(struct bufferevent *bev, void *ctx) {
  Server *server = (Server *)ctx;
  // 2048 may be too small for large msg
  char tmp[2048] = {0};
  size_t rpc_header_size = 4;
  size_t n = bufferevent_read(bev, tmp, rpc_header_size);
  assert(n == rpc_header_size);

  uint32_t payload_size = 0;
  for (int i = 0; i < rpc_header_size - 1; i++) {
    payload_size = payload_size | ((tmp[i] & 0xFFFFFFFF) << (i * 8));
  }
  MessageType msg_type = (MessageType)(tmp[3] & 0x000000FF);

  printf("payload size: %u\n", payload_size);
  printf("msg type: %d\n", msg_type);

  n = bufferevent_read(bev, tmp, payload_size);
  assert(n == payload_size);

  std::unique_ptr<Transport> trans = std::make_unique<SocketTransport>(bev);

  if (msg_type == MsgTypeAppend) {
    AppendEntriesMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);

  } else if (msg_type == MsgTypeAppendReply) {
    AppenEntriesReplyMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);

  } else if (msg_type == MsgTypeClient) {
    ClientMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
    server->m_mpreplica.handle_operation(msg, std::move(trans));

  } else if (msg_type == MsgTypeGuidance) {
    GuidanceMessage msg;
    auto oh = msgpack::unpack(tmp, payload_size);
    oh.get().convert(msg);
  }
}

void read_cb(struct bufferevent *bev, void *ctx) {
  printf("read_cb\n");

  parse_and_feed_msg(bev, ctx);
}

void event_cb(struct bufferevent *bev, short what, void *ctx) {
  int needfree = 0;

  if (what & BEV_EVENT_EOF || what & BEV_EVENT_TIMEOUT ||
      what & BEV_EVENT_ERROR) {
    needfree = 1;
  }

  if (needfree) {
    int errcode = evutil_socket_geterror(bufferevent_getfd(bev));
    printf("errcode: %d\n", errcode);
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
  printf("new bufferevent at %p\n", bev);

  bufferevent_setcb(bev, read_cb, nullptr, event_cb, ctx);
  bufferevent_enable(bev, EV_READ | EV_PERSIST);
}

void signal_cb(evutil_socket_t sig, short events, void *arg) {
  Server *server = (Server *)arg;
  struct timeval delay = {2, 0};

  printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

  for (auto item : server->m_peer_retry) {
    if (item.first != server->m_me) {
      event_del(item.second.m_retry_ev);
    }
  }
  event_base_loopexit(server->m_base, &delay);
}

void Server::start() {
  struct event_base *base = event_base_new();
  m_base = base;

  struct sockaddr saddr;
  int socklen = sizeof(saddr);

  int ret = evutil_parse_sockaddr_port("0.0.0.0:40713", &saddr, &socklen);
  assert(ret == 0);

  struct evconnlistener *lev = evconnlistener_new_bind(
      base, listener_cb, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 100,
      &saddr, socklen);
  assert(lev);

  struct event *ev_signal = evsignal_new(base, SIGINT, signal_cb, this);
  event_add(ev_signal, NULL);

  for (auto pid : m_peers) {
    if (pid != m_me) {
      periodic_peer(pid);
    }
  }

  event_base_dispatch(base);

  evconnlistener_free(lev);

  event_base_free(base);
}

void Server::connect_peer(int id) {
  bufferevent *bev = bufferevent_socket_new(m_base, -1, BEV_OPT_CLOSE_ON_FREE);
  struct sockaddr_in peer_addr;
  memset(&peer_addr, 0, sizeof(struct sockaddr_in));
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_port = htons(m_peer_port[id]);
  inet_aton(m_peer_addresses[id].c_str(), &peer_addr.sin_addr);
  bufferevent_socket_connect(bev, (struct sockaddr *)&peer_addr,
                             sizeof(struct sockaddr_in));

  m_peer_trans[id] = std::make_unique<SocketTransport>(bev);
}

void Server::periodic_peer(int id) {
  struct event *ev_period = event_new(
      m_base, -1, EV_TIMEOUT,
      [](evutil_socket_t fd, short what, void *arg) {
        // Server *server = (Server *)arg;
        PeerContext *ctx = (PeerContext *)arg;
        printf("to %d, what = 0x%x\n", ctx->id, what);

        struct timeval one_sec = {1, 0};
        event_add(ctx->m_retry_ev, &one_sec);
      },
      &m_peer_retry[id]);
  struct timeval one_sec = {1, 0};
  m_peer_retry[id].m_retry_ev = ev_period;
  event_add(ev_period, &one_sec);
}

int main(int argc, char **argv) {
  int id = 0;
  std::vector<int> peers{0, 1, 2};
  Server server(id, peers);

  server.m_peer_addresses[0] = std::string("127.0.0.1");
  server.m_peer_addresses[1] = std::string("127.0.0.1");
  server.m_peer_addresses[2] = std::string("127.0.0.1");

  server.m_peer_port[0] = 9990;
  server.m_peer_port[1] = 9991;
  server.m_peer_port[2] = 9992;

  server.start();
}