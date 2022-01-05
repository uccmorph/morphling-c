
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
#include "utils.h"

// each thread runs 1 client, for simplicity
int cfg_total_clients = 4;
int cfg_client_grouping = 1;
std::vector<std::string> cfg_replica_addr;
std::vector<int> cfg_replica_port;
bool cfg_is_read = false;
int cfg_msg_nums = 5;
bool cfg_unreplicated_read = false;

int cfg_value_size = 100;
static uint8_t *g_value;

std::atomic_int g_sent_nums(0);

std::vector<Gauge> g_latency_gauge;

void on_connection_event(struct bufferevent *bev, short what, void *arg);
void handle_reply(struct bufferevent *bev, void *ctx);
void send_new_msg(struct bufferevent *bev, void *ctx);
void handle_ur_reply(struct bufferevent *bev, void *arg);

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"total", required_argument, nullptr, 0},
      {"group", required_argument, nullptr, 0},
      {"replicas", required_argument, nullptr, 0},
      {"ro", no_argument, &has_default, 0},
      {"vs", required_argument, nullptr, 0},
      {"nums", required_argument, nullptr, 0},
      {"ur", no_argument, &has_default, 0},
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

          case 6:
            cfg_unreplicated_read = true;
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
  int m_id = 0;
  std::vector<Connection> m_conns;
  event_base *m_ev_base;
  event *m_ev_finish = nullptr;
  Guidance m_leading_guidance;
  std::vector<Guidance> m_one_turn_collection;

  uint8_t *m_data = nullptr;
  size_t m_data_size = 0;
  // uint64_t m_leading_epoch = 0;
  // bool m_idle = false;
  // bool m_is_reading = false;
  // bool m_is_writing = false;
  int max_replica_id = 3;
  int quorum = 0;
  int send_msg_nums = 0;
  int recv_msg_nums = 0;
  bool finish_all_msg = false;

 public:
  enum status_t {
    init,
    reading,
    waiting_guidance,
    writing,
    idle,
  };
  status_t status = status_t::init;
  Gauge *gauge;

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
  void set_id(int id) {
    m_id = id;
  }
  int get_id() {
    return m_id;
  }
  void set_data(uint8_t *d, size_t size) {
    m_data = d;
    m_data_size = size;
  }
  bool can_send() {
    // atomic increment
    if (++g_sent_nums <= cfg_msg_nums) {
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
    // debug_print_guidance(&m_one_turn_collection[id]);
  }
  bool check_guidance_quorum() {
    if (m_leading_guidance.epoch == 0) {
      LOG_F(WARNING, "client %d doesn't have leading guidance", m_id);
      return false;
    }
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
    if (m_leading_guidance.epoch < (*vote).first) {
      LOG_F(WARNING, "leading epoch %zu is smaller than other replica's %zu",
            m_leading_guidance.epoch, (*vote).first);
      return false;
    }
    if (m_leading_guidance.epoch > (*vote).first) {
      LOG_F(WARNING, "other replicas not reply proper guidance");
      return false;
    }

    if ((*vote).second >= quorum) {
      return true;
    }
    return false;
  }
  bool has_uniform_guidance() {
    uint64_t fe = m_one_turn_collection[0].epoch;
    if (fe == 0) {
      return false;
    }
    for (auto &guide : m_one_turn_collection) {
      if (guide.epoch != fe) {
        return false;
      }
    }
    return true;
  }
  bool update_leading_guidance() {
    uint64_t max = 0;
    int max_idx = 0;
    for (int i = 0; i < m_one_turn_collection.size(); i++) {
      if (m_one_turn_collection[i].epoch > max) {
        max = m_one_turn_collection[i].epoch;
        max_idx = i;
      }
    }

    int count = 0;
    for (int i = 0; i < m_one_turn_collection.size(); i++) {
      if (m_one_turn_collection[i].epoch == max) {
        count += 1;
      }
    }
    if (count >= quorum) {
      m_leading_guidance = m_one_turn_collection[max_idx];
      return true;
    }
    return false;
  }

  void reset_turn_collection() {
    m_one_turn_collection.clear();
    m_one_turn_collection.resize(max_replica_id);
    // m_leading_epoch = 0;
  }
  // void set_leading_epoch(uint64_t epoch) {
  //   m_leading_epoch = epoch;
  // }
  // uint64_t get_leading_epoch() {
  //   return m_leading_epoch;
  // }
  Guidance& peek_local_guidance() {
    return m_leading_guidance;
  }

  void build_comm();
  void request_guidance(int id);
  void one_send();
  void finish_event();
  void handle_reply(Connection *conn);
  void handle_ur_reply(Connection *conn, int type, uint8_t *msg_buf, size_t msg_size);
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

  if (cfg_unreplicated_read) {
    bufferevent_setcb(sock_bev, handle_ur_reply, nullptr, on_connection_event, this);
  } else {
    bufferevent_setcb(sock_bev, handle_reply, nullptr, on_connection_event, this);
  }
  bufferevent_enable(sock_bev, EV_READ | EV_PERSIST);

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
  // if (!is_idle()) {
  if (status != status_t::idle) {
    LOG_F(ERROR, "client %d has a outstanding request, can not send more", m_id);
    return;
  }
  if (!can_send()) {
    LOG_F(WARNING, "client %d all message has sent, can not send more", m_id);
    return;
  }
  uint8_t buf[2048];
  gauge->set_probe1();

#if 1
  Operation op{
      .op_type = cfg_is_read ? 0 : 1,
      .key_hash = 0x5499,
  };
  if (op.op_type == 1) {
    op.data.resize(m_data_size);
    LOG_F(INFO, "m_data at %p", m_data);
    std::copy(m_data, m_data + m_data_size, op.data.begin());
  }
  ClientMessage msg{
      .epoch = 1,
      .key_hash = 0x5499,
  };
  size_t total_size = 0;
  pack_operation(op, msg, buf, total_size);

  // send operation to replicable node
  int rid = m_leading_guidance.map_node_id(calc_key_pos(msg.key_hash));
  if (rid == -1) {
    LOG_F(FATAL, "don't have replicable node for key 0x%x, pos: 0x%x",
          msg.key_hash, calc_key_pos(msg.key_hash));
  }
  LOG_F(INFO, "client %d send operation %d to replica %d, size: %zu", m_id,
        op.op_type, rid, total_size);

  m_conns[rid].send(buf, total_size);
#endif

  if (cfg_unreplicated_read) {
    status = status_t::reading;
  } else {
    if (op.op_type == 0) {
      // m_is_reading = true;
      status = status_t::reading;
      for (int id = 0; id < max_replica_id; id++) {
        if (id == rid) {
          continue;
        }
        request_guidance(id);
      }
    } else {
      // m_is_writing = true;
      status = status_t::writing;
    }
  }
}

void Client::request_guidance(int id) {
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

void Client::handle_ur_reply(Connection *conn, int type, uint8_t *msg_buf,
                             size_t msg_size) {
  if (type == MsgTypeGuidance) {
    GuidanceMessage *msg = (GuidanceMessage *)msg_buf;
    set_guidance(conn->get_replica_id(), msg->guide);
  } else if (type == MsgTypeClientReply) {
    LOG_F(INFO,
          "===== client %d status %d receive new MsgTypeClientReply =====",
          m_id, status);
    ClientReplyMessage msg;
    auto oh = msgpack::unpack((char *)msg_buf, msg_size);
    oh.get().convert(msg);

    if (status == Client::status_t::reading) {
      status = Client::status_t::idle;
    } else if (status == Client::status_t::writing) {
      LOG_F(ERROR, "unreplicated read won't have writing");
    }
    LOG_F(INFO, "status: %d, success: %d, data size: %zu", status, msg.success,
          msg.reply_data.size());
  } else {
    LOG_F(ERROR, "type: %d not a client message", type);
  }
}

/* --------------- libevent callback --------------- */

void on_connection_event(struct bufferevent *bev, short what, void *arg) {
  Connection *conn = (Connection *)arg;
  if (what & BEV_EVENT_CONNECTED) {
    LOG_F(INFO, "connect to %d finish", conn->get_replica_id());
    conn->client->request_guidance(conn->get_replica_id());
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
  int id = conn->client->get_id();

  if (type == MsgTypeGuidance) {
    LOG_F(INFO, "===== client %d status %d receive new MsgTypeGuidance =====", id, conn->client->status);
    GuidanceMessage *msg = (GuidanceMessage *)buf;
    conn->client->set_guidance(conn->get_replica_id(), msg->guide);
    if (conn->client->status == Client::status_t::waiting_guidance) {
      if (conn->client->check_guidance_quorum()) {
        conn->client->reset_turn_collection();
        conn->client->status = Client::status_t::idle;
      }
    }
  } else if (type == MsgTypeClientReply) {
    LOG_F(INFO, "===== client %d status %d receive new MsgTypeClientReply =====", id, conn->client->status);
    ClientReplyMessage msg;
    auto oh = msgpack::unpack((char *)buf, msg_size);
    oh.get().convert(msg);

    if (conn->client->status == Client::status_t::reading) {
      conn->client->set_guidance(conn->get_replica_id(), msg.guidance);
      if (conn->client->check_guidance_quorum()) {
        conn->client->reset_turn_collection();
        conn->client->status = Client::status_t::idle;
      } else {
        conn->client->status = Client::status_t::waiting_guidance;
      }
    } else if (conn->client->status == Client::status_t::writing) {
      conn->client->reset_turn_collection();
      conn->client->status = Client::status_t::idle;
      conn->client->gauge->set_probe2();
      conn->client->gauge->instant_time_us();
    }
    LOG_F(INFO, "status: %d, success: %d, data size: %zu", conn->client->status, msg.success, msg.reply_data.size());
  } else {
    LOG_F(ERROR, "type: %d not a client message", type);
  }
}

void handle_ur_reply(struct bufferevent *bev, void *arg) {
  Connection *conn = (Connection *)arg;
  uint8_t buf[2048];
  size_t msg_size;
  auto type = recv_msg(bev, buf, msg_size);

  conn->client->handle_ur_reply(conn, type, buf, msg_size);
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

Client* one_ready_client(std::vector<Client> &clients) {
  for (auto &c : clients) {
    if (c.status == Client::status_t::idle) {
      return &c;
    }
  }
  return nullptr;
}

bool check_setup(std::vector<Client> &clients) {
  for (auto &c : clients) {
    if (!c.has_uniform_guidance()) {
      return false;
    } else {
      if (!c.update_leading_guidance()) {
        LOG_F(FATAL, "client %d update leading guidance fail", c.get_id());
      }
      // LOG_F(INFO, "client use guidance:");
      // debug_print_guidance(&c.peek_local_guidance());
      c.status = Client::status_t::idle;
    }
  }
  LOG_F(INFO, "all clients finish setup");
  return true;
}

struct finish_ctx {
  int count = 0;
  event_base *base;
};



void thread_run(int tid) {
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
    c.set_id(i);
    c.set_data(g_value, cfg_value_size);
    c.set_finish_event(finish_ev);
    c.gauge = &g_latency_gauge[tid * cfg_client_grouping + i];
    c.gauge->set_disable(true);
    c.build_comm();
  }

  while (true) {
    auto res = event_base_loop(base, EVLOOP_ONCE);
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
    Client *ready_c = one_ready_client(clients);
    if (ready_c == nullptr) {
      LOG_F(WARNING, "no client is idle");
    }

    if (ready_c != nullptr) {
      ready_c->one_send();
    }

#if 1
    // todo: break the loop only when all clients receive reply
    if (ready_c != nullptr && ready_c->has_finish_all()) {
      bool all_idle = true;
      for (auto &c : clients) {
        if (c.status != Client::status_t::idle) {
          all_idle = false;
          break;
        }
      }
      if (all_idle) {
        LOG_F(INFO, "all requests are finished");
        break;
      }
    }
    auto res = event_base_loop(base, EVLOOP_ONCE);
    LOG_F(INFO, "finish one loop");
    if (res == -1) {
      LOG_F(ERROR, "exit event loop with error. code %d. %s", errno,
            std::strerror(errno));
      break;
    }
    if (res == 1) {
      LOG_F(WARNING, "exit event loop");
      break;
    }
#endif

#if 0
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
        if (ready_c->status == Client::status_t::idle) {
          LOG_F(INFO, "client becomes idle");
          break;
        }
      }
    }

    if (ready_c != nullptr && ready_c->has_finish_all()) {
      LOG_F(INFO, "all requests are finished");
      break;
    }
#endif
  }

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
  Gauge gauge;
  g_latency_gauge.resize(cfg_total_clients);
  gauge.set_probe1();
  int thread_nums = cfg_total_clients / cfg_client_grouping;
  std::vector<std::thread> threads;
  threads.reserve(thread_nums);
  for (int i = 0; i < thread_nums; i++) {
    threads.emplace_back(
        [](int tid) {
          std::string t_name("thread id");
          t_name += " " + std::to_string(tid);
          loguru::set_thread_name(t_name.c_str());
          ERROR_CONTEXT("parent context", loguru::get_thread_ec_handle());
          thread_run(tid);
        },
        i);
  }

  for (auto &t : threads) {
    t.join();
  }
  gauge.set_probe2();
  gauge.total_time_ms(cfg_msg_nums);
  // for (int i = 0; i < cfg_total_clients; i++) {
  //   printf("latency client %d\n", i);
  //   g_latency_gauge[i].index_time_us();
  // }
}