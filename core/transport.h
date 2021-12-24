#ifndef __CORE_TRANSPORT_H__
#define __CORE_TRANSPORT_H__

#include <cstdint>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

class Transport {
public:
  virtual ~Transport() {}
  virtual void send(uint8_t *buf, uint64_t size) = 0;
};

#endif // __CORE_TRANSPORT_H__