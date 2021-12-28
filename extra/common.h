#ifndef __EXTRA_COMMON_H__
#define __EXTRA_COMMON_H__

#include <cstdint>

struct Operation {
  int type;
  uint64_t key;
  uint8_t value[64];
};

struct ClientMessage {
  int from;
  Operation op;
};

#endif // __EXTRA_COMMON_H__