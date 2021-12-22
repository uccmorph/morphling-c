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

int main(int c, char **v) {
  const char *server_ip = "127.0.0.1";
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

  /* Write the query. */
  /* XXX Can send succeed partially? */
  const char *query = "hello world";
  cp = query;
  remaining = strlen(query);
  while (remaining) {
    n_written = send(fd, cp, remaining, 0);
    if (n_written <= 0) {
      perror("send");
      return 1;
    }
    remaining -= n_written;
    cp += n_written;
    printf("send %zu bytes, remaining %zu\n", n_written, remaining);
  }

  memset(buf, 0, 1024);
  int readn = recv(fd, (void *)buf, 1024, 0);
  printf("receive %d bytes: %s\n", readn, buf);

  close(fd);
  return 0;
}