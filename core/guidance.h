#ifndef __CORE_GUIDANCE_H__
#define __CORE_GUIDANCE_H__

#include <cstdint>
#ifndef HARD_CODE_REPLICAS
#define HARD_CODE_REPLICAS 3
#endif

struct __attribute__((__packed__)) node_status_t {
  uint8_t start_key_pos;
  uint8_t end_key_pos;
  uint8_t alive;
};

struct __attribute__((__packed__)) guidance_t {
  uint64_t epoch;
  uint8_t alive_num;
  uint8_t cluster_size;
  node_status_t cluster[HARD_CODE_REPLICAS];
};


#endif //__CORE_GUIDANCE_H__