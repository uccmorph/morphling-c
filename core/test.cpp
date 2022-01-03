#include <stdio.h>
#include <stdlib.h>

#include "guidance.h"
Guidance g_guidance;
int main() {
  Guidance *g = &g_guidance;
  printf("g at %p, size: %zu\n", g, sizeof(Guidance));
  printf("status at: %p, %p and %p\n", &g->cluster[0], &g->cluster[1],
         &g->cluster[2]);

  g->set_cluster_size(3);
  g->set_alive_num(3);
  for (int i = 0; i < 3; i++) {
    g->cluster[i].set_pos(100*i, 100*i+50);
    g->cluster[i].set_alive();
  }
  debug_print_guidance(g);
}