#include <stdio.h>
#include <stdlib.h>

#include "guidance.h"
Guidance g_guidance;

static void debug_print_guidance(Guidance *g) {
  printf("g at %p, size: %zu\n", g, sizeof(Guidance));
  printf("status at: %p, %p and %p\n", &g->cluster[0], &g->cluster[1],
         &g->cluster[2]);
  printf("%s\n", g->to_string().c_str());
}

int main() {
  Guidance *g = &g_guidance;
  printf("g at %p, size: %zu\n", g, sizeof(Guidance));
  printf("status at: %p, %p and %p\n", &g->cluster[0], &g->cluster[1],
         &g->cluster[2]);

  g->cluster_size = 3;
  g->alive_num = 3;
  for (int i = 0; i < 3; i++) {
    g->cluster[i].start_pos = 100*i;
    g->cluster[i].end_pos = 100 * (i + 1) - 1;
    g->cluster[i].alive = 1;
  }
  debug_print_guidance(g);
}