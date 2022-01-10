#include <assert.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <functional>
#include <unordered_map>

void new_udp_event(evutil_socket_t fd, short what, void *arg);

enum MessageType {
  Client,
  Nested,
  NestedReply,
  ClientReply,
};

struct MessageHeader {
  uint32_t size:24;
  uint32_t type:8;
};

template <typename T>
std::pair<uint8_t *, size_t> msg_cast(T &msg) {
  return {reinterpret_cast<uint8_t *>(&msg), sizeof(msg)};
}

template <typename T>
T *msg_buf_cast(uint8_t *buf, size_t size) {
  if (sizeof(T) != size) {
    return nullptr;
  }
  return reinterpret_cast<T *>(buf);
}

struct Message {
  MessageHeader header;
};

struct ClientMessage: public Message {
  int client_id;

  ClientMessage() {
    header.type = MessageType::Client;
    header.size = sizeof(ClientMessage);
  }
};

struct NestedMessage: public Message {
  int data;
  std::vector<uint8_t> binary;

  NestedMessage() {
    header.type = MessageType::Nested;
    header.size = sizeof(NestedMessage);
  }
};

struct NestedReplyMessage: public Message {
  int data;

  NestedReplyMessage() {
    header.type = MessageType::NestedReply;
    header.size = sizeof(NestedReplyMessage);
  }
};

struct ClientReplyMessage: public Message {
  int data;

  ClientReplyMessage() {
    header.type = MessageType::ClientReply;
    header.size = sizeof(ClientReplyMessage);
  }
};


#if 0
int main() {
  NestedReplyMessage reply;
  reply.header.size = sizeof(reply);
  reply.header.type = MessageType::NestedReply;
  reply.data = 100;

  printf("NestedReplyMessage size: %zu\n", sizeof(reply));
  auto [buf, size] = msg_cast(reply);
  for (int i = 0; i < size; i++) {
    printf("0x%x ", buf[i]);
  }
  printf("\n");

  auto *recast_reply = msg_buf_cast<NestedReplyMessage>(buf, size);
  printf("size: %u, type: %u, data: %d\n", recast_reply->header.size,
    recast_reply->header.type, recast_reply->data);
}
#endif