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

struct NodeStatus {
  // uint8_t start_key_pos;
  // uint8_t end_key_pos;
  // uint8_t alive;
  /*
    0x000000FF for start_key_pos
    0x0000FF00 for end_key_pos
    0x00FF0000 for alive and other flags
  */
  uint32_t status;
  uint8_t get_start_pos() {
    return status & node_status_mask_start;
  }
  uint8_t get_end_pos() {
    return (status & node_status_mask_end) >> node_status_offset_end;
  }
  uint8_t alive() {
    return (status & node_status_mask_flags) >> node_status_offset_flags;
  }
  void set_pos(uint8_t start, uint8_t end) {
    uint16_t pos = start | (end << node_status_offset_end);
    status &= 0xFFFF0000;
    status |= pos;
  }
  void set_alive() {
    status &= 0x0000FFFF;
    status |= 0x00010000;
  }
  void set_dead() {
    status &= 0x0000FFFF;
  }
  bool pos_is_in(uint8_t pos) {
    if (!alive()) {
      return false;
    }
    auto start = get_start_pos();
    auto end = get_end_pos();
    if (start > end) {
      if (start <= pos) {
        return true;
      } else if (pos <= end) {
        return true;
      }
    } else {
      if (start <= pos && pos <= end) {
        return true;
      }
    }
    return false;
  }

};

struct Guidance {
  uint64_t epoch;
  // uint8_t alive_num;
  // uint8_t cluster_size;
  uint32_t status; // 0x0000FF00 for cluster_size, 0x000000FF for alive_num
  NodeStatus cluster[HARD_CODE_REPLICAS];
  // MSGPACK_DEFINE(epoch, status, cluster);
  uint8_t get_cluster_size() {
    return (status & 0xFF00) >> 8;
  }
  uint8_t get_alive_num() {
    return status & 0xFF;
  }
  void set_cluster_size(uint8_t size) {
    status &= 0x00FF;
    status |= (size << 8);
  }
  void set_alive_num(uint8_t num) {
    status &= 0xFF00;
    status |= num;
  }
  int map_node_id(uint8_t pos) {
    for (int id = 0; id < HARD_CODE_REPLICAS; id++) {
      if (cluster[id].pos_is_in(pos)) {
        return id;
      }
    }
    return -1;
  }
};

inline void debug_print_guidance(Guidance *g) {
  printf("g at %p, size: %zu\n", g, sizeof(Guidance));
  printf("status at: %p, %p and %p\n", &g->cluster[0], &g->cluster[1],
         &g->cluster[2]);
  printf("epoch: %zu, status: 0x%08x\n", g->epoch, g->status);
  for (int i = 0; i < 3; i++) {
    printf("i = %d, node status: 0x%08x\n", i, g->cluster[i].status);
  }
}


#endif //__CORE_GUIDANCE_H__