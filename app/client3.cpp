
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

void handle_reply(evutil_socket_t fd, short what, void *arg);
void handle_ur_reply(evutil_socket_t fd, short what, void *arg);

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
class Channel {
  friend Client;

  event_base *ev_base;
  int replica_id;
  std::string replica_ip;
  std::string replica_port;

  int fd;
  sockaddr_in udp_dest;

public:
  Client *client;

public:
  void build_event();

  MessageType recv(uint8_t *buf, size_t &size);
  void send(uint8_t *buf, size_t size);
};

class Client {
  int m_id = 0;
  event_base *m_ev_base;

  Guidance m_leading_guidance;
  std::vector<Guidance> m_one_turn_collection;
  std::vector<Channel> m_channel;
  uint8_t *m_data = nullptr;
  size_t m_data_size = 0;

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
  Client(event_base *base) : m_ev_base(base) {}
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

  void set_channel_info(std::vector<std::string> ips, std::vector <int> ports) {
    max_replica_id = (int)cfg_replica_port.size();
    quorum = max_replica_id / 2 + 1;
    m_channel.resize(ips.size());
    for (size_t id = 0; id < ips.size(); id++) {
      m_channel[id].replica_id = id;
      m_channel[id].replica_ip = ips[id];
      m_channel[id].replica_port = ports[id];
      m_channel[id].ev_base = m_ev_base;
      m_channel[id].client = this;

      m_channel[id].fd = socket(AF_INET, SOCK_DGRAM, 0);
      m_channel[id].udp_dest.sin_family = AF_INET;
      m_channel[id].udp_dest.sin_addr.s_addr = inet_addr(ips[id].c_str());
      m_channel[id].udp_dest.sin_port = htons(ports[id]);
    }

    m_one_turn_collection.resize(max_replica_id);
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
    for (size_t i = 0; i < m_one_turn_collection.size(); i++) {
      if (m_one_turn_collection[i].epoch > max) {
        max = m_one_turn_collection[i].epoch;
        max_idx = i;
      }
    }

    int count = 0;
    for (size_t i = 0; i < m_one_turn_collection.size(); i++) {
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
  }

  Guidance& peek_local_guidance() {
    return m_leading_guidance;
  }

  void build_comm();
  void request_guidance(int id);
  void one_send();
  void handle_reply(Channel *chan, int type, uint8_t *msg_buf, size_t msg_size);
  void handle_ur_reply(Channel *conn, int type, uint8_t *msg_buf, size_t msg_size);
};

/* --------------- Channel --------------- */

void Channel::build_event() {
  event_callback_fn cb;
  if (cfg_unreplicated_read) {
    cb = handle_ur_reply;
  } else {
    cb = handle_reply;
  }
  event *ev = event_new(ev_base, fd, EV_READ | EV_PERSIST, cb, this);
  event_add(ev, nullptr);
}

MessageType Channel::recv(uint8_t *buf, size_t &size) {
  MessageHeader header;
  uint8_t *header_tmp = reinterpret_cast<uint8_t *>(&header);
  socklen_t addr_len = sizeof(udp_dest);
  int read_n = recvfrom(fd, header_tmp, sizeof(header), MSG_PEEK, (sockaddr *)&udp_dest, &addr_len);
  assert(read_n == (int)sizeof(header));

  size = sizeof(header) + header.size;
  read_n = recvfrom(fd, buf, size, 0, (sockaddr *)&udp_dest, &addr_len);
  assert(read_n == (int)size);

  return (MessageType)header.type;
}

void Channel::send(uint8_t *buf, size_t size) {
  int send_n = sendto(fd, buf, size, 0, (sockaddr *)&udp_dest, sizeof(udp_dest));
  assert(send_n == (int)size);
}

/* --------------- Client --------------- */

void Client::one_send() {
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
    LOG_F(FATAL, "don't have replicable node for key 0x%lx, pos: 0x%x",
          msg.key_hash, calc_key_pos(msg.key_hash));
  }
  LOG_F(INFO, "client %d send operation %d to replica %d, pos 0x%x, size: %zu", m_id,
        op.op_type, rid, calc_key_pos(msg.key_hash), total_size);

  m_channel[rid].send(buf, total_size);
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
      status = status_t::writing;
    }
  }
}

void Client::request_guidance(int id) {
  LOG_F(INFO, "send get_guidance to replica %d", id);
  uint8_t buf[64];
  size_t msg_size;
  pack_get_guiddance(buf, msg_size);
  m_channel[id].send(buf, msg_size);
  // need to handle and add on reply later.
}

void Client::build_comm() {
  for (auto &chan : m_channel) {
    chan.build_event();
    request_guidance(chan.replica_id);
  }
}

void Client::handle_reply(Channel *chan, int type, uint8_t *msg_buf, size_t msg_size) {
  if (type == MsgTypeGuidance) {
    LOG_F(INFO, "===== client %d status %d receive new MsgTypeGuidance =====", m_id, status);
    GuidanceMessage *msg = (GuidanceMessage *)msg_buf;
    set_guidance(chan->replica_id, msg->guide);
    if (status == Client::status_t::waiting_guidance) {
      if (check_guidance_quorum()) {
        reset_turn_collection();
        status = Client::status_t::idle;
      }
    }
  } else if (type == MsgTypeClientReply) {
    LOG_F(INFO, "===== client %d status %d receive new MsgTypeClientReply =====", m_id, status);
    ClientReplyMessage msg;
    auto oh = msgpack::unpack((char *)msg_buf, msg_size);
    oh.get().convert(msg);

    if (status == Client::status_t::reading) {
      set_guidance(chan->replica_id, msg.guidance);
      if (check_guidance_quorum()) {
        reset_turn_collection();
        status = Client::status_t::idle;
      } else {
        status = Client::status_t::waiting_guidance;
      }
    } else if (status == Client::status_t::writing) {
      reset_turn_collection();
      status = Client::status_t::idle;
      gauge->set_probe2();
      gauge->instant_time_us();
    }
    LOG_F(INFO, "status: %d, success: %d, data size: %zu", status, msg.success, msg.reply_data.size());
  } else {
    LOG_F(ERROR, "type: %d not a client message", type);
  }
}

void Client::handle_ur_reply(Channel *chan, int type, uint8_t *msg_buf,
                             size_t msg_size) {
  if (type == MsgTypeGuidance) {
    GuidanceMessage *msg = (GuidanceMessage *)msg_buf;
    set_guidance(chan->replica_id, msg->guide);
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

void handle_reply(evutil_socket_t fd, short what, void *arg) {
  if ((what & EV_READ) == 0) {
    LOG_F(FATAL, "should have a read event");
  }
  Channel *chan = (Channel *)arg;
  uint8_t buf[2048];
  size_t msg_size;
  auto type = chan->recv(buf, msg_size);

  uint8_t *tmp = buf + sizeof(MessageHeader);
  msg_size -= sizeof(MessageHeader);
  chan->client->handle_reply(chan, type, tmp, msg_size);
}

void handle_ur_reply(evutil_socket_t fd, short what, void *arg) {
  Channel *chan = (Channel *)arg;
  uint8_t buf[2048];
  size_t msg_size;
  auto type = chan->recv(buf, msg_size);

  uint8_t *tmp = buf + sizeof(MessageHeader);
  msg_size -= sizeof(MessageHeader);
  chan->client->handle_ur_reply(chan, type, tmp, msg_size);
}

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

void thread_run(int tid) {
  event_base *base = event_base_new();

  std::vector<Client> clients;
  clients.reserve(cfg_client_grouping);
  for (int i = 0; i < cfg_client_grouping; i++) {
    Client &c = clients.emplace_back(base);
    c.set_id(i);
    c.set_data(g_value, cfg_value_size);
    c.set_channel_info(cfg_replica_addr, cfg_replica_port);
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
  }

  // event_base_free(base);
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

  g_latency_gauge.resize(cfg_total_clients);
  int thread_nums = cfg_total_clients / cfg_client_grouping;

  Gauge gauge;
  gauge.set_probe1();
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

}