/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <assert.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINE 16384

struct Context {
  struct event_base *base;
};

Context g_ctx;

void do_read(evutil_socket_t fd, short events, void *arg);
void do_write(evutil_socket_t fd, short events, void *arg);

static void pSigHandler(int signo) {
  printf("catch signal no: %d\n", signo);
  switch (signo) {
    case SIGKILL:
    case SIGTERM:
    case SIGHUP:
    case SIGINT:
      event_base_free(g_ctx.base);
      printf("quit all\n");
      break;
  }
  
}

char rot13_char(char c) {
  /* We don't want to use isalpha here; setting the locale would change
   * which characters are considered alphabetical. */
  if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
    return c + 13;
  else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
    return c - 13;
  else
    return c;
}

void readcb(struct bufferevent *bev, void *ctx) {
  struct evbuffer *input, *output;
  char *line;
  size_t n;
  int i;
  input = bufferevent_get_input(bev);
  output = bufferevent_get_output(bev);
  printf("inter %s\n", __func__);

  while ((line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF))) {
    for (i = 0; i < n; ++i) line[i] = rot13_char(line[i]);
    evbuffer_add(output, line, n);
    evbuffer_add(output, "\n", 1);
    free(line);
  }

  if (evbuffer_get_length(input) >= MAX_LINE) {
    /* Too long; just process what there is and go on so that the buffer
     * doesn't grow infinitely long. */
    char buf[1024];
    while (evbuffer_get_length(input)) {
      int n = evbuffer_remove(input, buf, sizeof(buf));
      for (i = 0; i < n; ++i) buf[i] = rot13_char(buf[i]);
      evbuffer_add(output, buf, n);
    }
    evbuffer_add(output, "\n", 1);
  }
}

void errorcb(struct bufferevent *bev, short error, void *ctx) {
  if (error & BEV_EVENT_EOF) {
    /* connection has been closed, do any clean up here */
    /* ... */
    printf("connection has been closed, do any clean up here\n");
  } else if (error & BEV_EVENT_ERROR) {
    /* check errno to see what error occurred */
    /* ... */
    printf("check errno [%d] to see what error occurred\n", errno);
  } else if (error & BEV_EVENT_TIMEOUT) {
    /* must be a timeout event handle, handle it */
    /* ... */
    printf("must be a timeout event handle, handle it\n");
  }
  bufferevent_free(bev);
}

void do_accept(evutil_socket_t listener, short event, void *arg) {
  struct event_base *base = (struct event_base *)arg;
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  int fd = accept(listener, (struct sockaddr *)&ss, &slen);
  if (fd < 0) {
    perror("accept");
  } else if (fd > FD_SETSIZE) {
    close(fd);
  } else {
    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, readcb, NULL, errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
  }
}

void run(void) {
  evutil_socket_t listener;
  struct sockaddr_in sin;
  struct event_base *base;
  struct event *listener_event;

  struct sigaction psa;
  psa.sa_handler = pSigHandler;
  sigaction(SIGKILL, &psa, NULL);
  sigaction(SIGINT, &psa, NULL);
  sigaction(SIGTERM, &psa, NULL);
  sigaction(SIGHUP, &psa, NULL);

  base = event_base_new();
  if (!base) return; /*XXXerr*/

  g_ctx.base = base;

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = 0;
  sin.sin_port = htons(40713);

  listener = socket(AF_INET, SOCK_STREAM, 0);
  evutil_make_socket_nonblocking(listener);

  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  if (bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
    perror("bind");
    return;
  }

  if (listen(listener, 16) < 0) {
    perror("listen");
    return;
  }
  printf("server start\n");

  listener_event =
      event_new(base, listener, EV_READ | EV_PERSIST, do_accept, (void *)base);
  /*XXX check it */
  event_add(listener_event, NULL);

  event_base_dispatch(base);

  // event_base_free(base);
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  run();

  return 0;
}
