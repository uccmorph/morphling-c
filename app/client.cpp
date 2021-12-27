/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <string>
#include <chrono>

#include <msgpack.hpp>
#include <loguru.hpp>
#include "message.h"
#include "smr.h"

int port = 9990;
std::string ip;
int send_nums = 0;
bool is_read = false;

bool parse_cmd(int argc, char **argv) {
  int has_default = 0;
  static struct option long_options[] = {
      {"ip", required_argument, nullptr, 0},
      {"port", required_argument, nullptr, 0},
      {"nums", required_argument, nullptr, 0},
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
            ip = optarg;
            LOG_F(1, "ip = %s\n", ip.c_str());
            mandatory += 1;
            break;
          case 1:
            port = std::stoi(optarg);
            LOG_F(1, "port = %d\n", port);
            mandatory += 1;
            break;
          case 2:
            send_nums = std::stoi(optarg);
            LOG_F(1, "total number %d\n", send_nums);
            mandatory += 1;
            break;
          case 3:
            is_read = true;
            break;
        }

        break;
      default:
        LOG_F(ERROR, "unknown arg: %c\n", c);
        fail = true;
      
    }
  }

  
  if (fail || mandatory != 3) {
    printf("need --ip, --port, --nums\n");
    return false;
  }
  return true;
}

std::string default_data(int size) {
  std::string res;
  for (int i = 0; i < size; i++) {
    res.append("a");
  }
  return res;
}

int send_loop(int fd, std::string &query) {
  char buf[2048];
  /* Write the query. */
  /* XXX Can send succeed partially? */
  Operation op{
    .op_type = is_read ? 0 : 1,
    .key_hash = 0x5499,
    .data = std::vector<uint8_t>(query.begin(), query.end()),
  };
  std::stringstream ss;
  msgpack::pack(ss, op);
  const std::string &op_str = ss.str();
  ClientMessage msg{
    .epoch = 1,
    .key_hash = 0x5499,
    .op = std::vector<uint8_t>(op_str.begin(), op_str.end()),
  };
  LOG_F(3, "op buf size: %zu\n", op_str.size());

  std::stringstream ss2;
  msgpack::pack(ss2, msg);
  const std::string &msg_buf = ss2.str();
  uint32_t msg_size = msg_buf.size();
  LOG_F(3, "msg_size = 0x%08x\n", msg_size);
  for (int i = 0; i < 4; i++) {
    LOG_F(3, "after shift %d: %x\n", i * 2, (msg_size >> (i * 8)));
    buf[i] = (msg_size >> (i * 8)) & 0xFF;
    LOG_F(3, "buf %d is 0x%x\n", i, buf[i]);
  }
  buf[3] = MessageType::MsgTypeClient & 0xFF;

  assert(msg_buf.size() < 2000);
  memcpy(&buf[4], msg_buf.c_str(), msg_buf.size());
  int n = send(fd, buf, 4 + msg_buf.size(), 0);

  // n = send(fd, msg_buf.c_str(), msg_buf.size(), 0);
  LOG_F(3, "send real length: %zu\n", n);

  ClientMessage r_msg;
  auto oh = msgpack::unpack(msg_buf.c_str(), msg_buf.size());
  oh.get().convert(r_msg);
  LOG_F(3, "client: epoch: %zu, key_hash: %zu, op size: %zu\n", r_msg.epoch, r_msg.key_hash, r_msg.op.size());

  memset(buf, 0, 1024);
  int readn = recv(fd, (void *)buf, 1024, 0);
  LOG_F(3, "receive %d bytes: %s\n", readn, buf);
}

int run() {
  const char *server_ip = ip.c_str();
  struct sockaddr_in sin;
  int fd;
  
  int res = 0;

  /* Allocate a new socket */
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  /* Connect to the remote host. */
  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  res = inet_aton(server_ip, &sin.sin_addr);
  if (connect(fd, (struct sockaddr *)&sin, sizeof(sin))) {
    perror("connect");
    close(fd);
    return 1;
  }

  std::string query = default_data(1000);
  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

  for (int i = 0; i < send_nums; i++) {
    send_loop(fd, query);
  }
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  double diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
  diff *= 1e-9;

  LOG_F(INFO, "total test time: %f s", diff);

  close(fd);

  return 0;
}

int main(int argc, char **argv) {
  loguru::init(argc, argv);

  if (!parse_cmd(argc, argv)) {
    return 1;
  }

  run();
}