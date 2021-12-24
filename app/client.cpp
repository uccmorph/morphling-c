/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string>

#include <msgpack.hpp>
#include "message.h"
#include "smr.h"

int main(int c, char **v) {
  const char *server_ip = "127.0.0.1";
  struct sockaddr_in sin;

  const char *cp;
  int fd;
  ssize_t n_written, remaining;
  char buf[2048];
  int res = 0;

  /* Allocate a new socket */
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  /* Connect to the remote host. */
  sin.sin_family = AF_INET;
  sin.sin_port = htons(40713);
  res = inet_aton(server_ip, &sin.sin_addr);
  if (connect(fd, (struct sockaddr *)&sin, sizeof(sin))) {
    perror("connect");
    close(fd);
    return 1;
  }

  /* Write the query. */
  /* XXX Can send succeed partially? */
  const char *query = "hello world";
  Operation op{
    .op_type = 0,
    .key_hash = 0x5499,
  };
  std::stringstream ss;
  msgpack::pack(ss, op);
  const std::string &op_str = ss.str();
  ClientMessage msg{
    .epoch = 1,
    .key_hash = 0x5499,
    .op = std::vector<uint8_t>(op_str.begin(), op_str.end()),
  };
  printf("op buf size: %zu\n", op_str.size());

  std::stringstream ss2;
  msgpack::pack(ss2, msg);
  const std::string &msg_buf = ss2.str();
  uint32_t msg_size = msg_buf.size();
  printf("msg_size = 0x%08x\n", msg_size);
  for (int i = 0; i < 4; i++) {
    printf("after shift %d: %x\n", i * 2, (msg_size >> (i * 8)));
    buf[i] = (msg_size >> (i * 8)) & 0xFF;
    printf("buf %d is 0x%x\n", i, buf[i]);
  }
  buf[3] = MessageType::MsgTypeClient & 0xFF;

  assert(msg_buf.size() < 2000);
  memcpy(&buf[4], msg_buf.c_str(), msg_buf.size());
  int n = send(fd, buf, 4 + msg_buf.size(), 0);

  // n = send(fd, msg_buf.c_str(), msg_buf.size(), 0);
  printf("send real length: %zu\n", n);

  ClientMessage r_msg;
  auto oh = msgpack::unpack(msg_buf.c_str(), msg_buf.size());
  oh.get().convert(r_msg);
  printf("client: epoch: %zu, key_hash: %zu, op size: %zu\n", r_msg.epoch, r_msg.key_hash, r_msg.op.size());

  memset(buf, 0, 1024);
  int readn = recv(fd, (void *)buf, 1024, 0);
  printf("receive %d bytes: %s\n", readn, buf);

  close(fd);
  return 0;
}