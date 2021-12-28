
#include <getopt.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>
#include <thread>
#include <vector>

#include <loguru.hpp>

#include "message.h"
#include "utils.cpp"


// each thread runs 1 client, for simplicity
int cfg_total_clients = 4;
int cfg_client_grouping = 1;
std::vector<std::string> cfg_replica_addr;
std::vector<int> cfg_replica_port;
bool cfg_is_read = false;

class Connection {
 public:
  int fd;

  int connect(int replica_id) {
    struct sockaddr_in sin;
    fd = socket(AF_INET, SOCK_STREAM, 0);

    sin.sin_family = AF_INET;
    sin.sin_port = htons(cfg_replica_port[replica_id]);
    inet_aton(cfg_replica_addr[replica_id].c_str(), &sin.sin_addr);
    if (::connect(fd, (struct sockaddr *)&sin, sizeof(sin))) {
      perror("connect");
      close(fd);
      return 1;
    }
  }
};

class Client {
  int guide_vote = 0;
  int task_nums = 10;
  std::vector<Connection> conns;

public:
  Client() {
    for (int id = 0; id < cfg_replica_port.size(); id++) {
      conns.emplace_back(Connection());
    }
  }
  void set_task_nums(int nums) {
    task_nums = nums;
  }
  void one_send(uint8_t *data, size_t data_size);
  void build_comm();
};

void Client::one_send(uint8_t *data, size_t data_size) {
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
  for (int id = 0; id < cfg_replica_port.size(); id++) {
    Client::conns[id].connect(id);
  }
  
}

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"total", required_argument, nullptr, 0},
      {"group", required_argument, nullptr, 0},
      {"replicas", required_argument, nullptr, 0},
      {"ro", no_argument, &has_default, 0},
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
  for (int i = 0; i < cfg_replica_addr.size(); i++) {
    LOG_F(1, "cfg_replica_addr[%d] = %s", i, cfg_replica_addr[i].c_str());
  }
  LOG_F(1, "cfg_is_read = %d", cfg_is_read);
  return true;
}

void send_on_ready(Client& ready_client) {
  ready_client.one_send();
}

Client& wait_ready_client(std::vector<Client>& clients) {
  return clients[0];
}

void thread_run() {
  std::vector<Client> clients;
  clients.reserve(cfg_client_grouping);
  for (int i = 0; i < cfg_client_grouping; i++) {
    Client& c = clients.emplace_back(Client());
    c.build_comm();
  }
  LOG_F(INFO, "all clients connected");
  while (true) {
    Client &ready_client = wait_ready_client(clients);
    send_on_ready(ready_client);
  }
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
    threads.emplace_back(std::thread([](int i) {
      std::string t_name("thread id");
      t_name += " " + std::to_string(i);
      loguru::set_thread_name(t_name.c_str());
      ERROR_CONTEXT("parent context", loguru::get_thread_ec_handle());
      thread_run();
    }, i));
  }

  for (auto &t : threads) {
    t.join();
  }
}