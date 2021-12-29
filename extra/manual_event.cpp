
#include <event2/event.h>
#include <stdio.h>



struct finish_ctx {
  int count = 0;
  event_base *base;
  event *embed_ev;
};

static void cb(int sock, short which, void *arg) {
  /* Whoops: Calling event_active on the same event unconditionally
     from within its callback means that no other events might not get
     run! */
  finish_ctx *fctx = (finish_ctx *)arg;
  printf("triger one event\n");

  // event_base_loopbreak(fctx->base);
  event_active(fctx->embed_ev, EV_WRITE, 0);
}

static void cb_embed(int sock, short which, void *arg) {
  /* Whoops: Calling event_active on the same event unconditionally
     from within its callback means that no other events might not get
     run! */
  finish_ctx *fctx = (finish_ctx *)arg;
  printf("triger embed event\n");

  event_base_loopbreak(fctx->base);
  // event_active(ev, EV_WRITE, 0);
}

int main(int argc, char **argv) {
  struct event_base *base = event_base_new();
  finish_ctx fctx;
  fctx.base = base;
  struct event *embed_ev = event_new(base, -1, EV_PERSIST | EV_READ, cb_embed, &fctx);
  event_add(embed_ev, nullptr);

  fctx.embed_ev = embed_ev;
  struct event *ev;
  ev = event_new(base, -1, EV_PERSIST | EV_READ, cb, &fctx);
  event_add(ev, nullptr);

  event_active(ev, EV_WRITE, 0);

  struct timeval three_sec = {3, 0};
  event_base_loopexit(base, &three_sec);

  printf("run base event loop\n");
  event_base_dispatch(base);

  return 0;
}