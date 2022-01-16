#ifndef __CORE_GUIDANCE_H__
#define __CORE_GUIDANCE_H__

#include <cstdint>
#include <sstream>
#include <iomanip>

#ifndef CONFIG_HARD_CODE_REPLICAS
#define CONFIG_HARD_CODE_REPLICAS 3
#endif

constexpr int ReplicaNumbers = CONFIG_HARD_CODE_REPLICAS;

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
  uint32_t start_pos:8;
  uint32_t end_pos:8;
  uint32_t alive:8;

  NodeStatus(): start_pos(0), end_pos(0), alive(0) {}

  bool pos_is_in(uint8_t pos) {
    if (alive == 0) {
      return false;
    }
    uint8_t start = start_pos;
    uint8_t end = end_pos;
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
  uint64_t term = 0;
  // uint8_t alive_num;
  // uint8_t cluster_size;
  // 0x000000FF for cluster_size, 0x0000FF00 for alive_num
  uint32_t cluster_size:8;
  uint32_t alive_num:8;
  NodeStatus cluster[ReplicaNumbers];

  int map_node_id(uint8_t pos) {
    for (int id = 0; id < ReplicaNumbers; id++) {
      if (cluster[id].pos_is_in(pos)) {
        return id;
      }
    }
    return -1;
  }

  std::string to_string() {
    std::stringstream ss;
    ss << "term: " << term << ", "
       << "cluster size: " << cluster_size << ", "
       << "alive: " << alive_num << std::endl;

    for (int i = 0; i < ReplicaNumbers; i++) {
      ss << "id: " << i << ", "
         << "status: 0x" << std::setfill('0') << std::setw(8) << std::hex
         << *reinterpret_cast<uint32_t *>(&cluster[i]) << "; ";
    }

    return std::move(ss.str());
  }
};




#endif //__CORE_GUIDANCE_H__