
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

#include "message.h"
#include "smr.h"
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
            g_value = new uint8_t[cfg_value_size];
            memset(g_value, 'a', cfg_value_size);
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

  int __replica_id;

 public:
  Client *m_client;

 public:
  Connection(event_base *base) : m_base(base) {}
  void set_client(Client *c) {
    m_client = c;
  }

  int get_replica_id() {
    return __replica_id;
  }

  void set_event_cb_on_connection();

  int connect(int replica_id);
};

class Client {
  int guide_vote = 0;
  int task_nums = 10;
  std::vector<Connection> conns;
  event_base *e_base;
  event *ev_finish;

  uint8_t *data;
  size_t data_size;
  int send_msg_nums = 0;
  int recv_msg_nums = 0;

 public:
  Client(event_base *base) : e_base(base) {
    for (size_t id = 0; id < cfg_replica_port.size(); id++) {
      auto &conn = conns.emplace_back(Connection(base));
      conn.set_client(this);
    }
  }
  void set_task_nums(int nums) { task_nums = nums; }
  void set_data(uint8_t *d, size_t size) {
    data = d;
    data_size = size;
  }
  bool add_send_nums() {
    if (send_msg_nums < cfg_msg_nums) {
      send_msg_nums += 1;
      return true;
    }
    return false;
  }
  bool add_recv_msg() {
    recv_msg_nums += 1;
    if (recv_msg_nums < cfg_msg_nums) {
      return true;
    }
    return false;
  }
  void set_finish_event(event *ev) {
    ev_finish = ev;
  }


  void one_send();
  void build_comm();
  void finish_event();
};

void Connection::set_event_cb_on_connection() {
  bufferevent_setcb(
      m_sock_bev, handle_reply, send_new_msg,
      [](struct bufferevent *bev, short what, void *arg) {
        LOG_F(INFO, "unexpected event in send task: 0x%x", what);
      }, this);
  bufferevent_enable(m_sock_bev, EV_READ | EV_WRITE | EV_PERSIST);
}

int Connection::connect(int replica_id) {
  struct sockaddr_in sin;
  __replica_id = replica_id;

  sin.sin_family = AF_INET;
  sin.sin_port = htons(cfg_replica_port[replica_id]);
  inet_aton(cfg_replica_addr[replica_id].c_str(), &sin.sin_addr);

  bufferevent *sock_bev =
      bufferevent_socket_new(m_base, -1, BEV_OPT_CLOSE_ON_FREE);
  m_sock_bev = sock_bev;
  bufferevent_socket_connect(sock_bev, (struct sockaddr *)&sin, sizeof(sin));

  bufferevent_setcb(sock_bev, nullptr, nullptr, on_connection_event, this);
  bufferevent_enable(sock_bev, EV_READ | EV_WRITE | EV_ET | EV_PERSIST);

  return 0;
}

void Client::one_send() {
  Operation op{
      .op_type = cfg_is_read ? 0 : 1,
      .key_hash = 0x5499,
  };
  op.data.reserve(data_size);
  std::copy(data, data + data_size, op.data.begin());
  ClientMessage msg{
      .epoch = 1,
      .key_hash = 0x5499,
  };
}

void Client::build_comm() {
  for (size_t id = 0; id < cfg_replica_port.size(); id++) {
    Client::conns[id].connect((int)id);
  }
}

void Client::finish_event() {
  if (ev_finish == nullptr) {
    LOG_F(FATAL, "finish ev is not registered");
  }
  event_active(ev_finish, EV_WRITE, 0);
}

void on_connection_event(struct bufferevent *bev, short what, void *arg) {
  Connection *conn = (Connection *)arg;
  if (what & BEV_EVENT_CONNECTED) {
    LOG_F(INFO, "connect to %d finish", conn->get_replica_id());
    // send_new_msg(bev, arg);
    // conn->m_client->finish_event();
  } else {
    LOG_F(INFO, "unexpected event: 0x%x", what);
  }
}

void handle_reply(struct bufferevent *bev, void *arg) {
  evbuffer *input = bufferevent_get_input(bev);
  char *res_str = evbuffer_readln(input, nullptr, EVBUFFER_EOL_CRLF);
  LOG_F(3, "read: %s", res_str);
  Connection *conn = (Connection *)arg;
  if (!conn->m_client->add_recv_msg()) {
    LOG_F(INFO, "receive all reply, should quit event loop");
    conn->m_client->finish_event();
  }

  send_new_msg(bev, arg);
}

void send_new_msg(struct bufferevent *bev, void *arg) {
  Connection *conn = (Connection *)arg;
  if (!conn->m_client->add_send_nums()) {
    LOG_F(INFO, "reach max send nums, wait reply");
    // bufferevent_disable(bev, EV_READ | EV_WRITE);
    return;
  }
  LOG_F(3, "we can send new msg");

  std::string mock_data("hello world!");

  int write_res;
  write_res = bufferevent_write(bev, mock_data.c_str(), mock_data.size());
  if (write_res != 0) {
    LOG_F(ERROR, "write bev failed: err code %d", errno);
  }
}

void send_on_ready(Client &ready_client) { ready_client.one_send(); }

Client &wait_ready_client(std::vector<Client> &clients) { return clients[0]; }

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
    Client &c = clients.emplace_back(Client(base));
    c.set_data(g_value, cfg_value_size);
    c.build_comm();
    c.set_finish_event(finish_ev);
  }
  // LOG_F(INFO, "all clients connected");
  // while (true) {
  //   Client &ready_client = wait_ready_client(clients);
  //   send_on_ready(ready_client);
  // }

  struct timeval three_sec = {3, 0};
  event_base_loopexit(base, &three_sec);
  event_base_dispatch(base);
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