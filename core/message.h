#ifndef __CORE_MESSAGE_H__
#define __CORE_MESSAGE_H__

#include <cstdint>
#include <msgpack.hpp>
#include <vector>

#include "guidance.h"

enum MessageType {
  MsgTypeUnknown = -1,
  MsgTypeAppend,
  MsgTypeAppendReply,
  MsgTypeClient,
  MsgTypeClientReply,
  MsgTypeGuidance,
  MsgTypeGetGuidance,
};

struct Message {
  virtual std::unique_ptr<std::vector<uint8_t>> serialize();
  virtual void deserialize(uint8_t *data, size_t size);
};

struct MessageHeader {
  uint32_t size : 24;
  uint32_t type : 8;
};

struct EntryRaw {
  uint64_t term = 0;
  uint64_t index = 0;
  size_t data_size;

};

struct Entry {
  uint64_t term = 0;
  uint64_t index = 0;
  std::vector<uint8_t> data;
  Entry() {}
  Entry(uint64_t _epoch, uint64_t _index): term(_epoch), index(_index) {}
  Entry(std::vector<uint8_t> &buf): data(buf.begin(), buf.end()) {}

  std::string debug() {
    return "term: " + std::to_string(term) + ", index: " + std::to_string(index) +
           ", data size: " + std::to_string(data.size());
  }
};

struct AppendEntriesRawMessage {
  MessageHeader header;

  int from = 0;
  uint64_t term = 0;
  uint64_t prev_term = 0;
  uint64_t prev_index = 0;
  uint64_t commit = 0;
  uint64_t group_id = 0;

  EntryRaw entry;

  void copy_entry_data_in(uint8_t *buf, size_t size) {
    size_t offset = sizeof(AppendEntriesRawMessage);
    uint8_t *body = (uint8_t *)this;
    memcpy(body + offset, buf, size);
    assert(entry.data_size == size);
  }

  void copy_entry_data_out(uint8_t *buf, size_t size) {
    size_t offset = sizeof(AppendEntriesRawMessage);
    uint8_t *body = (uint8_t *)this;
    memcpy(buf, body + offset, size);
    assert(entry.data_size == size);
  }
};

struct AppendEntriesMessage {
  int from = 0;
  uint64_t term = 0;

  uint64_t prev_term = 0;
  uint64_t prev_index = 0;
  uint64_t commit = 0;
  uint64_t group_id = 0;
  Entry entry;

  std::string debug() {
    std::stringstream ss;
    ss << "from: " << from << ", " <<
    "term: " << term << ", " <<
    "prev_term: " << prev_term << "," <<
    "prev_index: " << prev_index << "," <<
    "commit: " << commit << "," <<
    "group_id: " << group_id << "," <<
    "entry index: " << entry.index << ", " <<
    "entry term: " << entry.term << ", " <<
    "entry data size: " << entry.data.size();

    return std::move(ss.str());
  }
};

struct AppendEntriesReplyMessage {
  MessageHeader header;

  int from;
  uint64_t term;
  bool success;
  uint64_t group_id;
  uint64_t index;
};

struct Operation {
  int op_type;  // 0 for read, 1 for write
  uint64_t key_hash;
  std::vector<uint8_t> data;
};

// pack/unpack helper
struct OperationRaw {
  int op_type;  // 0 for read, 1 for write
  uint64_t key_hash;
  size_t data_size;
};

struct ClientMessage {
  // Guidance guidance;
  uint64_t term;
  uint64_t key_hash;
  std::vector<uint8_t> op;
};

struct ClientRawMessge {
  MessageHeader header;

  uint64_t term;
  uint64_t key_hash;
  size_t data_size;
};

// struct ClientReplyMessage {
//   int from;
//   bool success;
//   Guidance guidance;
//   uint64_t key_hash;
//   std::vector<uint8_t> reply_data;
// };

struct ClientReplyRawMessage {
  MessageHeader header;

  int from;
  bool success;
  Guidance guidance;
  uint64_t key_hash;
  size_t data_size;

  void copy_data_in(uint8_t *buf, size_t size) {
    size_t offset = sizeof(ClientReplyRawMessage);
    uint8_t *body = (uint8_t *)this;
    memcpy(body + offset, buf, size);
    assert(data_size == size);
  }

  void copy_data_out(uint8_t *buf, size_t size) {
    size_t offset = sizeof(ClientReplyRawMessage);
    uint8_t *body = (uint8_t *)this;
    memcpy(buf, body + offset, size);
    assert(data_size == size);
  }
};

struct GuidanceMessage {
  MessageHeader header;
  int from;
  int votes;
  Guidance guide;
};

// struct GenericMessage {
//   MessageType type;
//   int to;
//   AppendEntriesMessage append_msg;
//   AppendEntriesReplyMessage append_reply_msg;
//   ClientMessage client_msg;
//   GuidanceMessage guidance_msg;

//   GenericMessage(AppendEntriesMessage &msg, int to)
//       : type(MsgTypeAppend), to(to), append_msg(msg) {}
//   GenericMessage(AppendEntriesReplyMessage &msg, int to)
//       : type(MsgTypeAppendReply), to(to), append_reply_msg(msg) {}
//   GenericMessage(ClientMessage &msg, int to)
//       : type(MsgTypeClient), to(to), client_msg(msg) {}
//   GenericMessage(GuidanceMessage &msg, int to)
//       : type(MsgTypeGuidance), to(to), guidance_msg(msg) {}
// };

#endif  // __CORE_MESSAGE_H__