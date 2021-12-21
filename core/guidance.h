#ifndef __CORE_GUIDANCE_H__
#define __CORE_GUIDANCE_H__

#include <cstdint>
#include <stdio.h>
#include <msgpack.hpp>

#ifndef HARD_CODE_REPLICAS
#define HARD_CODE_REPLICAS 3
#endif

constexpr uint32_t guidance_mask_alive = 0xFF;
constexpr uint32_t guidance_offset_cluster = 8;
constexpr uint32_t guidance_mask_cluster_size = 0xFF << guidance_offset_cluster;

constexpr uint32_t node_status_mask_start = 0xFF;
constexpr uint32_t node_status_offset_end = 8;
constexpr uint32_t node_status_mask_end = 0xFF << 8;
constexpr uint32_t node_status_offset_flags = 16;
constexpr uint32_t node_status_mask_flags = 0xFF << node_status_offset_flags;

struct node_status_t {
  // uint8_t start_key_pos;
  // uint8_t end_key_pos;
  // uint8_t alive;
  /*
    0x000000FF for start_key_pos
    0x0000FF00 for end_key_pos
    0x00FF0000 for alive and other flags
  */  
  uint32_t status;
  MSGPACK_DEFINE(status);
};

struct guidance_t {
  uint64_t epoch;
  // uint8_t alive_num;
  // uint8_t cluster_size;
  uint32_t status; // 0x0000FF00 for cluster_size, 0x000000FF for alive_num
  node_status_t cluster[HARD_CODE_REPLICAS];
  MSGPACK_DEFINE(epoch, status, cluster);
};

inline void debug_print_guidance(guidance_t *g) {
  printf("g at %p, size: %zu\n", g, sizeof(guidance_t));
  printf("status at: %p, %p and %p\n", &g->cluster[0], &g->cluster[1],
         &g->cluster[2]);
  printf("epoch: %zu, status: 0x%08x\n", g->epoch, g->status);
  for (int i = 0; i < 3; i++) {
    printf("i = %d, node status: 0x%08x\n", i, g->cluster[i].status);
  }
}


#endif //__CORE_GUIDANCE_H__