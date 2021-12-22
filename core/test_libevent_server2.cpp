#include <assert.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct client_ctx_st {
  struct event_base* base;
  int shutting;
};

int set_linger(int fd, int onoff, int linger) {
  struct linger l = {.l_onoff = onoff, .l_linger = linger};
  int res = setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
  assert(res == 0);
  return res;
}

int set_keepalive(int fd, int keepalive, int cnt, int idle, int intvl) {
  int res =
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
  assert(res == 0);

  res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &idle, sizeof(idle));
  assert(res == 0);

  res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  assert(res == 0);

  res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
  assert(res == 0);

  return res;
}

void read_cb(struct bufferevent* bev, void* ctx) {
  struct client_ctx_st* client_ctx = (struct client_ctx_st*)ctx;
  printf("read_cb\n");

  int fd = bufferevent_getfd(bev);
  struct evbuffer* input = bufferevent_get_input(bev);
  struct evbuffer* output = bufferevent_get_output(bev);
  char* line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY);
  if (line) {
    if (strcmp(line, "quit") == 0) {
      if (evbuffer_get_length(output) > 0)
        client_ctx->shutting = 1;
      else
        shutdown(fd, SHUT_RD);
    } else if (strcmp(line, "abort") == 0) {
      shutdown(fd, SHUT_RD);
    }
    if (!client_ctx->shutting) evbuffer_add_printf(output, "%s\n", line);
    free(line);
  }
}

void write_cb(struct bufferevent* bev, void* ctx) {
  struct client_ctx_st* client_ctx = (struct client_ctx_st*)ctx;
  printf("write_cb\n");

  int fd = bufferevent_getfd(bev);
  struct evbuffer* input = bufferevent_get_input(bev);
  struct evbuffer* output = bufferevent_get_output(bev);

  if (evbuffer_get_length(output) == 0 && client_ctx->shutting)
    shutdown(fd, SHUT_RD);
}

void event_cb(struct bufferevent* bev, short what, void* ctx) {
  struct client_ctx_st* client_ctx = (struct client_ctx_st*)ctx;
  int needfree = 0;

  if (what & BEV_EVENT_READING) {
    if (what & BEV_EVENT_EOF || what & BEV_EVENT_TIMEOUT) needfree = 1;
  }

  if (what & BEV_EVENT_ERROR) {
    needfree = 1;
  }

  if (needfree) {
    int errcode = evutil_socket_geterror(bufferevent_getfd(bev));
    printf("errcode:%d\n", errcode);
    bufferevent_free(bev);
    free(client_ctx);
  }
}

void listener_cb(struct evconnlistener* l, evutil_socket_t nfd,
                 struct sockaddr* addr, int socklen, void* ctx) {
  struct event_base* base = (struct event_base*)ctx;

  set_linger(nfd, 1, 0);
  set_keepalive(nfd, 1, 1, 5, 5);

  struct bufferevent* bev =
      bufferevent_socket_new(base, nfd, BEV_OPT_CLOSE_ON_FREE);
  assert(bev);

  struct client_ctx_st* client_ctx = (struct client_ctx_st*)malloc(sizeof(struct client_ctx_st));
  client_ctx->base = base;
  client_ctx->shutting = 0;

  bufferevent_setcb(bev, read_cb, nullptr, event_cb, client_ctx);
  bufferevent_enable(bev, EV_READ | EV_WRITE | EV_ET);
}

int main(void) {
  struct event_base* base = event_base_new();

  struct sockaddr saddr;
  int socklen = sizeof(saddr);

  int ret = evutil_parse_sockaddr_port("0.0.0.0:40713", &saddr, &socklen);
  assert(ret == 0);

  struct evconnlistener* lev = evconnlistener_new_bind(
      base, listener_cb, base, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 100,
      &saddr, socklen);
  assert(lev);

  event_base_dispatch(base);

  evconnlistener_free(lev);

  event_base_free(base);
}