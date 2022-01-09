#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For gethostbyname */
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>

#include <vector>
#include <chrono>
#include <string>

int cfg_loop_times = 5;
int cfg_send_size = 128;

bool parse_cmd(int argc, char **argv) {
int has_default = 0;
  static struct option long_options[] = {
      {"lt", required_argument, nullptr, 0},
      {"size", required_argument, nullptr, 0},
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
            cfg_loop_times = std::stoi(optarg);
            break;

          case 1:
            cfg_send_size = std::stoi(optarg);
            break;
        }

        break;
      default:
        printf("unknown arg: %c", c);
        fail = true;
    }
  }

  if (fail) {
    return false;
  }

  return true;
}

uint8_t* prepare_data() {
  uint8_t *query = new uint8_t[cfg_send_size];
  memset(query, 'a', cfg_send_size);

  return query;
}

int send_multiple(int fd, const char *server_ip, int server_port) {
  auto query = prepare_data();

  struct sockaddr_in udp_dest;
  int res = 0;
  char buf[1024];
  memset(&udp_dest, 0, sizeof(udp_dest));
  udp_dest.sin_family = AF_INET;
  udp_dest.sin_addr.s_addr = inet_addr(server_ip);
  udp_dest.sin_port = htons(server_port);

  for (int i = 0; i < cfg_loop_times; i++) {
    res = sendto(fd, query, cfg_send_size, 0, (sockaddr *)&udp_dest, sizeof(udp_dest));
    if (res == -1) {
      printf("send error: %d, %s\n", errno, strerror(errno));
      return res;
    }
    printf("send to %s:%d size %d\n", inet_ntoa(udp_dest.sin_addr),
           ntohs(udp_dest.sin_port), res);
  }

  return 0;
}

int send_and_recv(int fd, const char *server_ip, int server_port) {
  auto query = prepare_data();

  struct sockaddr_in udp_dest;
  int res = 0;
  char buf[1024];
  memset(&udp_dest, 0, sizeof(udp_dest));
  udp_dest.sin_family = AF_INET;
  udp_dest.sin_addr.s_addr = inet_addr(server_ip);
  udp_dest.sin_port = htons(server_port);
  res = sendto(fd, query, cfg_send_size, 0, (sockaddr *)&udp_dest, sizeof(udp_dest));
  if (res == -1) {
    printf("send error: %d, %s\n", errno, strerror(errno));
    return res;
  }
  printf("send to %s:%d\n", inet_ntoa(udp_dest.sin_addr), ntohs(udp_dest.sin_port));

  socklen_t addr_len;
  recvfrom(fd, buf, 1024, 0, (sockaddr *)&udp_dest, &addr_len);
  printf("recv from %s:%d\n", inet_ntoa(udp_dest.sin_addr), ntohs(udp_dest.sin_port));

  return res;
}


int main(int argc, char **argv) {

  if (!parse_cmd(argc, argv)) {
    return 1;
  }

  const char *server_ip = "127.0.0.1";
  int server_port = 40713;


  const char *cp;
  int fd;

  int res = 0;

  /* Allocate a new socket */
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  send_multiple(fd, server_ip, server_port);
}