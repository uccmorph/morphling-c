
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <loguru.hpp>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <map>

#include "message.h"
#include "smr.h"
#include "morphling.h"
#include "utils.cpp"

// each thread runs 1 client, for simplicity
int cfg_total_clients = 4;
int cfg_client_grouping = 1;
std::vector<std::string> cfg_replica_addr;
std::vector<int> cfg_replica_port;
bool cfg_is_read = false;
int cfg_msg_nums = 5;

int cfg_value_size = 100;
static uint8_t *g_value;

std::atomic_int g_sent_nums(0);

void on_connection_event(struct bufferevent *bev, short what, void *arg);
void handle_reply(struct bufferevent *bev, void *ctx);
void send_new_msg(struct bufferevent *bev, void *ctx);

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"total", required_argument, nullptr, 0},
      {"group", required_argument, nullptr, 0},
      {"replicas", required_argument, nullptr, 0},
      {"ro", no_argument, &has_default, 0},
      {"vs", required_argument, nullptr, 0},
      {"nums", required_argument, nullptr, 0},
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
            cfg_total_clients = std::stoi(optarg);
            break;

          case 1:
            cfg_client_grouping = std::stoi(optarg);
            break;

          case 2:
            mandatory += 1;
            parse_addr(optarg, cfg_replica_addr);
            break;

          case 3:
            cfg_is_read = true;
            break;

          case 4:
            cfg_value_size = std::stoi(optarg);
            break;

          case 5:
            cfg_msg_nums = std::stoi(optarg);
            break;
        }

        break;
      default:
        LOG_F(ERROR, "unknown arg: %c", c);
        fail = true;
    }
  }

  if (fail || mandatory != 1) {
    LOG_F(ERROR, "need --replicas");
    return false;
  }

  g_value = new uint8_t[cfg_value_size];
  memset(g_value, 'a', cfg_value_size);
  LOG_F(1, "cfg_total_clients = %d", cfg_total_clients);
  LOG_F(1, "cfg_client_grouping = %d", cfg_client_grouping);
  for (size_t i = 0; i < cfg_replica_addr.size(); i++) {
    LOG_F(1, "cfg_replica_addr[%d] = %s", (int)i, cfg_replica_addr[i].c_str());
  }
  LOG_F(1, "cfg_is_read = %d", cfg_is_read);
  LOG_F(1, "cfg_value_size = %d", cfg_value_size);
  LOG_F(1, "cfg_msg_nums = %d", cfg_msg_nums);
  return true;
}

class Client;
class Connection {
  event_base *m_base;
  bufferevent *m_sock_bev;

  int m_replica_id;

 public:
  Client *client;
  bool has_pending_reply = false;

 public:
  Connection(event_base *base, int id) : m_base(base), m_replica_id(id) {}
  void set_client(Client *c) {
    LOG_F(INFO, "set client to %p", c);
    client = c;
  }
  int get_replica_id() {
    return m_replica_id;
  }

  int connect();
  void send(uint8_t *buf, size_t size);
};

class Client {
  int guide_vote = 0;
  int m_task_nums = 10;
  std::vector<Connection> m_conns;
  event_base *m_ev_base;
  event *m_ev_finish = nullptr;
  Guidance m_guidace;
  std::vector<Guidance> m_one_turn_collection;

  uint8_t *m_data = nullptr;
  size_t m_data_size = 0;
  uint64_t m_leading_epoch = 0;
  bool m_idle = false;
  int max_replica_id = 3;
  int quorum = 0;
  int send_msg_nums = 0;
  int recv_msg_nums = 0;
  bool finish_all_msg = false;

 private:


 public:
  Client(event_base *base) : m_ev_base(base) {
    max_replica_id = (int)cfg_replica_port.size();
    quorum = max_replica_id / 2 + 1;

    m_conns.reserve(max_replica_id);
    for (int id = 0; id < max_replica_id; id++) {
      auto &conn = m_conns.emplace_back(Connection(base, id));
      conn.set_client(this);
    }

    m_one_turn_collection.resize(max_replica_id);
  }
  void set_data(uint8_t *d, size_t size) {
    m_data = d;
    m_data_size = size;
  }
  bool can_send() {
    if (g_sent_nums++ <= cfg_msg_nums) {
      send_msg_nums += 1;
      return true;
    }
    finish_all_msg = true;
    return false;
  }
  bool has_finish_all() {
    return finish_all_msg;
  }
  void set_finish_event(event *ev) {
    m_ev_finish = ev;
  }
  void set_guidance(int id, Guidance &guide) {
    LOG_F(INFO, "set new guidance of replica: %d", id);
    // m_guidace = guide;
    m_one_turn_collection[id] = guide;
    debug_print_guidance(&m_one_turn_collection[id]);
  }
  bool check_guidance_quorum() {
    std::map<uint64_t, int> epoch_votes;
    for (auto &guide : m_one_turn_collection) {
      epoch_votes[guide.epoch] += 1;
      LOG_F(INFO, "add 1 vote to epoch %zu, curr votes: %d", guide.epoch,
            epoch_votes[guide.epoch]);
    }
    auto vote = epoch_votes.rbegin();
    if ((*vote).first == 0) {
      return false;
    }
    if (m_leading_epoch != 0 && m_leading_epoch < (*vote).first) {
      LOG_F(WARNING, "leading epoch %zu is smaller than other replica's %zu",
            m_leading_epoch, (*vote).first);
      return false;
    }
    if (m_leading_epoch > (*vote).first) {
      LOG_F(WARNING, "other replicas not reply proper guidance");
      return false;
    }

    if ((*vote).second >= quorum) {
      m_idle = true;
      for (auto &guide : m_one_turn_collection) {
        if (guide.epoch == (*vote).first) {
          m_guidace = guide;
        }
      }
      return true;
    }
    return false;
  }
  void reset_turn_collection() {
    m_one_turn_collection.clear();
    m_one_turn_collection.resize(max_replica_id);
    m_leading_epoch = 0;
  }
  void set_leading_epoch(uint64_t epoch) {
    m_leading_epoch = epoch;
  }
  uint64_t get_leading_epoch() {
    return m_leading_epoch;
  }
  bool is_idle() {
    return m_idle;
  }


  void build_comm();
  void get_guidance(int id);
  void one_send();
  void finish_event();


};

/* --------------- Connection --------------- */

int Connection::connect() {
  struct sockaddr_in sin;

  sin.sin_family = AF_INET;
  sin.sin_port = htons(cfg_replica_port[m_replica_id]);
  inet_aton(cfg_replica_addr[m_replica_id].c_str(), &sin.sin_addr);

  LOG_F(INFO, "client at: %p", client);
  bufferevent *sock_bev =
      bufferevent_socket_new(m_base, -1, BEV_OPT_CLOSE_ON_FREE);
  m_sock_bev = sock_bev;
  bufferevent_socket_connect(sock_bev, (struct sockaddr *)&sin, sizeof(sin));

  bufferevent_setcb(sock_bev, handle_reply, nullptr, on_connection_event, this);
  bufferevent_enable(sock_bev, EV_READ | EV_ET | EV_PERSIST);

  return 0;
}

void Connection::send(uint8_t *buf, size_t size) {
  int write_res;
  write_res = bufferevent_write(m_sock_bev, buf, size);
  if (write_res != 0) {
    LOG_F(ERROR, "write bev failed: err code %d", errno);
  }
}

/* --------------- Client --------------- */



void Client::one_send() {
  if (!m_idle) {
    LOG_F(ERROR, "this client has a outstanding request, can not send more");
    return;
  }
  if (!can_send()) {
    LOG_F(INFO, "all message has sent, can not send more");
    return;
  }
  m_idle = false;
  uint8_t buf[2048];

#if 1
  Operation op{
      .op_type = cfg_is_read ? 0 : 1,
      .key_hash = 0x5499,
  };
  op.data.resize(m_data_size);
  LOG_F(INFO, "m_data at %p", m_data);
  std::copy(m_data, m_data + m_data_size, op.data.begin());
  ClientMessage msg{
      .epoch = 1,
      .key_hash = 0x5499,
  };
  size_t total_size = 0;
  pack_operation(op, msg, buf, total_size);

  // send operation to replicable node
  int rid = m_guidace.map_node_id(calc_key_pos(msg.key_hash));
  if (rid == -1) {
    LOG_F(FATAL, "don't have replicable node for key 0x%x, pos: 0x%x",
          msg.key_hash, calc_key_pos(msg.key_hash));
  }
  LOG_F(INFO, "send operation to replica %d, size: %zu", rid, total_size);
  for (size_t i = 0; i < total_size; i++) {
    printf("0x%x ", buf[i]);
  }
  printf("\n");
  m_conns[rid].send(buf, total_size);
#endif

  for (int id = 0; id < max_replica_id; id++) {
    if (id == rid) {
      continue;
    }
    get_guidance(id);
  }
}

void Client::get_guidance(int id) {
  LOG_F(INFO, "send get_guidance to replica %d", id);
  uint8_t buf[64];
  size_t msg_size;
  pack_get_guiddance(buf, msg_size);
  m_conns[id].send(buf, msg_size);
  // need to handle and add on reply later.
}

void Client::build_comm() {
  for (auto &conn : m_conns) {
    conn.connect();
  }
}

void Client::finish_event() {
  if (m_ev_finish == nullptr) {
    LOG_F(FATAL, "finish ev is not registered");
  }
  event_active(m_ev_finish, EV_WRITE, 0);
}

/* --------------- libevent callback --------------- */

void on_connection_event(struct bufferevent *bev, short what, void *arg) {
  Connection *conn = (Connection *)arg;
  if (what & BEV_EVENT_CONNECTED) {
    LOG_F(INFO, "connect to %d finish", conn->get_replica_id());
    conn->client->get_guidance(conn->get_replica_id());
  } else {
    LOG_F(INFO, "conn %d, unexpected event: 0x%x", conn->get_replica_id(), what);
    if (what & BEV_EVENT_ERROR || what & BEV_EVENT_EOF) {
      LOG_F(ERROR, "EOF or error[%d]: %s", errno, std::strerror(errno));
      bufferevent_free(bev);
      // conn->client->finish_event();
    }
  }
}

void handle_reply(struct bufferevent *bev, void *arg) {
  Connection *conn = (Connection *)arg;
  uint8_t buf[2048];
  size_t msg_size;
  auto type = recv_msg(bev, buf, msg_size);

  if (type == MsgTypeGuidance) {
    LOG_F(INFO, "receive new MsgTypeGuidance");
    GuidanceMessage *msg = (GuidanceMessage *)buf;
    conn->client->set_guidance(conn->get_replica_id(), msg->guide);
  } else if (type == MsgTypeClient) {
    LOG_F(INFO, "receive new MsgTypeClient");
    ClientMessage msg;
    auto oh = msgpack::unpack((char *)buf, msg_size);
    oh.get().convert(msg);
    Guidance leading_guide;
    leading_guide.epoch = msg.epoch;
    conn->client->set_leading_epoch(msg.epoch);
    conn->client->set_guidance(conn->get_replica_id(), leading_guide);
  } else {
    LOG_F(ERROR, "type: %d not a client message", type);
  }

}

#if 0
void send_new_msg(struct bufferevent *bev, void *arg) {
  Connection *conn = (Connection *)arg;
  if (!conn->client->add_send_nums()) {
    LOG_F(INFO, "reach max send nums, wait reply");
    return;
  }
  LOG_F(3, "we can send new msg");

  std::string mock_data("hello world!");

  conn->send((uint8_t *)mock_data.c_str(), mock_data.size());
}
#endif

/* --------------- multi-thread --------------- */

void send_on_ready(Client &ready_client) { ready_client.one_send(); }

Client* one_ready_client(std::vector<Client> &clients) {
  for (auto &c : clients) {
    if (c.is_idle()) {
      return &c;
    }
  }
  return nullptr;
}

bool check_setup(std::vector<Client> &clients) {
  for (auto &c : clients) {
    if (!c.check_guidance_quorum()) {
      return false;
    }
  }
  LOG_F(INFO, "all clients finish setup");
  return true;
}



struct finish_ctx {
  int count = 0;
  event_base *base;
};

void thread_run() {
  event_base *base = event_base_new();
  finish_ctx fctx;
  fctx.base = base;
  event *finish_ev = event_new(base, -1, EV_READ | EV_PERSIST, [](int sock, short which, void *arg){
    finish_ctx *ctx = (finish_ctx *)arg;
    LOG_F(INFO, "one client is finished");

    ctx->count += 1;
    if (ctx->count >= cfg_client_grouping) {
      LOG_F(WARNING, "ready to break event loop");
      event_base_loopbreak(ctx->base);
    }
  }, &fctx);
  event_add(finish_ev, nullptr);

  std::vector<Client> clients;
  clients.reserve(cfg_client_grouping);
  for (int i = 0; i < cfg_client_grouping; i++) {
    Client &c = clients.emplace_back(base);
    c.set_data(g_value, cfg_value_size);
    c.set_finish_event(finish_ev);
    c.build_comm();
  }


#if 1
  while (true) {
    auto res = event_base_loop(base, EVLOOP_NONBLOCK);
    LOG_F(INFO, "finish one loop");
    if (res == -1) {
      LOG_F(ERROR, "exit event loop with error. code %d. %s", errno, std::strerror(errno));
      break;
    }
    if (res == 1) {
      LOG_F(WARNING, "exit event loop");
    }
    auto setup = check_setup(clients);
    if (!setup) {
      LOG_F(INFO, "continue loop");
    } else {
      break;
    }
  }

  while (true) {
    Client *ready_c = nullptr;
    ready_c = one_ready_client(clients);
    if (ready_c == nullptr) {
      LOG_F(FATAL, "current not support null client");
    }

    if (ready_c != nullptr) {
      ready_c->one_send();
    }
    while (true) {
      auto res = event_base_loop(base, EVLOOP_NONBLOCK);
      LOG_F(INFO, "finish one loop");
      if (res == -1) {
        LOG_F(ERROR, "exit event loop with error. code %d. %s", errno,
              std::strerror(errno));
        break;
      }
      if (res == 1) {
        LOG_F(WARNING, "exit event loop");
      }

      if (ready_c != nullptr) {
        auto get_q = ready_c->check_guidance_quorum();
        if (get_q) {
          break;
        }
      }
    }

    if (ready_c != nullptr && ready_c->has_finish_all()) {
      LOG_F(INFO, "all requests are finished");
      break;
    }
  }
#endif
  event_base_free(base);
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }
  // hard code like server
  cfg_replica_port.emplace_back(9990);
  cfg_replica_port.emplace_back(9991);
  cfg_replica_port.emplace_back(9992);

  int thread_nums = cfg_total_clients / cfg_client_grouping;
  std::vector<std::thread> threads;
  threads.reserve(thread_nums);
  for (int i = 0; i < thread_nums; i++) {
    threads.emplace_back(std::thread(
        [](int i) {
          std::string t_name("thread id");
          t_name += " " + std::to_string(i);
          loguru::set_thread_name(t_name.c_str());
          ERROR_CONTEXT("parent context", loguru::get_thread_ec_handle());
          thread_run();
        },
        i));
  }

  for (auto &t : threads) {
    t.join();
  }
}