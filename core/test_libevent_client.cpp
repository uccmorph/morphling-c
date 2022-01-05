/* For sockaddr_in */
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

#include <vector>
#include <chrono>


class Gauge {
  std::vector<std::chrono::steady_clock::time_point> measure_probe1;
  std::vector<std::chrono::steady_clock::time_point> measure_probe2;

public:
  Gauge() {
    measure_probe1.reserve(1000000);
    measure_probe2.reserve(1000000);
  }

  void set_probe1() {
    measure_probe1.emplace_back(std::chrono::steady_clock::now());
  }

  void set_probe2() {
    measure_probe2.emplace_back(std::chrono::steady_clock::now());
  }

  void total_time_ms(int total_msg) {
    if (measure_probe1.size() == 0 || measure_probe2.size() == 0) {
      printf("no metrics");
      return;
    }
    auto &start = *(measure_probe1.begin());
    auto &end = *(measure_probe2.rbegin());
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-6;
    printf("total time: %f ms, throughput: %f Kops\n", res, total_msg / res);
  }

  void index_time_us() {
    if (measure_probe1.size() != measure_probe2.size()) {
      printf("Two probe don't have same number of data (%zu : %zu)",
            measure_probe1.size(), measure_probe2.size());
      return;
    }
    for (size_t i = 0; i < measure_probe1.size(); i++) {
      double interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           measure_probe2[i] - measure_probe1[i])
                           .count();
      interval *= 1e-3;
      printf("loop %zu, interval: %f us\n", i, interval);
    }
  }

  void instant_time_us() {
    if (measure_probe1.size() != measure_probe2.size()) {
      printf("Two probe don't have same number of data (%zu : %zu)",
            measure_probe1.size(), measure_probe2.size());
      return;
    }
    auto &start = *(measure_probe1.rbegin());
    auto &end = *(measure_probe2.rbegin());
    double res =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    res *= 1e-3;
    printf("loop %zu, interval: %f us\n", measure_probe1.size(), res);
  }
};

int main(int c, char **v) {
  const char *server_ip = "10.1.6.233";
  struct sockaddr_in sin;

  const char *cp;
  int fd;
  ssize_t n_written, remaining;
  char buf[1024];
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
  Gauge gauge;

  /* Write the query. */
  /* XXX Can send succeed partially? */
  const int send_size = 10;
  char *query = new char[1000];
  for (int i = 0; i < send_size; i++) {
    query[i] = 'a';
  }

  for (int i = 0; i < 1000; i++) {
    cp = query;
    remaining = send_size;
    gauge.set_probe1();
    while (remaining) {
      n_written = send(fd, cp, remaining, 0);
      if (n_written <= 0) {
        perror("send");
        return 1;
      }
      remaining -= n_written;
      cp += n_written;
      // printf("send %zu bytes, remaining %zu\n", n_written, remaining);
    }

    memset(buf, 0, 1024);
    int readn = recv(fd, (void *)buf, 1024, 0);
    gauge.set_probe2();
    gauge.instant_time_us();
    // printf("receive %d bytes: %s\n", readn, buf);
  }

  close(fd);
  return 0;
}