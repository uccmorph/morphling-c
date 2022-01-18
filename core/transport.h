#ifndef __CORE_TRANSPORT_H__
#define __CORE_TRANSPORT_H__

#include <cstdint>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>


#include "message.h"

class Transport {
public:
  virtual ~Transport() {};
  virtual void send(uint8_t *send_buf, uint64_t size) = 0;
  virtual bool recv(uint8_t *recv_buf, uint64_t expect_size) = 0;
  virtual bool is_ready() = 0;
  // virtual void send(MessageType type, uint8_t *payload, uint64_t payload_size) = 0;
  // virtual void send(AppendEntriesMessage &msg) = 0;
  // virtual void send(AppendEntriesReplyMessage &msg) = 0;
  // virtual void send(ClientReplyMessage &msg) = 0;
  // virtual void send(GuidanceMessage &msg) = 0;
  // virtual void send(MessageType type, void *msg) = 0;
};

#endif // __CORE_TRANSPORT_H__