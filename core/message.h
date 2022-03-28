#ifndef __CORE_MESSAGE_H__
#define __CORE_MESSAGE_H__

#include <cstdint>
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
  MsgTypeGossip,
};

struct Message {
  virtual std::unique_ptr<std::vector<uint8_t>> serialize();
  virtual void deserialize(uint8_t *data, size_t size);
};

constexpr size_t PreAllocBufferSize = 4096;

// Warning: don't use copy constructor in any way, and be careful when construct it
// in map or vector.
template <typename T>
struct MessageBuffer {
  uint8_t *buf = nullptr;
  size_t size = 0;

  MessageBuffer(): buf(new uint8_t[PreAllocBufferSize]), size(PreAllocBufferSize) {}

  MessageBuffer(size_t size): size(size) {
    buf = new uint8_t[size];
  }

  ~MessageBuffer() {
    if (buf != nullptr) {
      delete []buf;
      buf = nullptr;
    }
  }

  void init(size_t size) {
    if (buf != nullptr) {
      delete []buf;
    }
    buf = new uint8_t[size];
  }

  T& to_message() {
    return *reinterpret_cast<T *>(buf);
  }

  MessageBuffer(MessageBuffer &obj) = delete;
  MessageBuffer& operator=(MessageBuffer &obj) = delete;
};

struct MessageHeader {
  // 0x00FFFFFF for size, 0xFF000000 for type
  uint32_t size : 24;
  uint32_t type : 8;
};

// struct Operation {
//   int op_type;  // 0 for read, 1 for write
//   uint64_t key_hash;
//   std::vector<uint8_t> data;
// };

// pack/unpack helper
struct OperationRaw {
  int op_type;  // 0 for read, 1 for write, 2 for Replicated-read
  uint64_t key_hash;
  size_t value_size;

  uint8_t* get_value_buf() {
    uint8_t *value_buf = reinterpret_cast<uint8_t *>(this);
    return (value_buf + sizeof(OperationRaw));
  }
};

struct EntryRaw {
  uint64_t epoch = 0;
  uint64_t index = 0;
  size_t data_size;

  uint8_t* get_op_buf() {
    uint8_t *op_buf = reinterpret_cast<uint8_t *>(this);
    return (op_buf + sizeof(EntryRaw));
  }

  OperationRaw& get_op() {
    uint8_t *op_buf = reinterpret_cast<uint8_t *>(this);
    op_buf += sizeof(EntryRaw);
    return *reinterpret_cast<OperationRaw *>(op_buf);
  }
};

struct Entry {
  uint64_t epoch = 0;
  uint64_t index = 0;
  std::vector<uint8_t> data;
  Entry() {}
  Entry(std::vector<uint8_t> &buf): data(buf.begin(), buf.end()) {}
  Entry(uint8_t *buf, size_t size): data(buf, buf + size) {}

  std::string debug() {
    return "epoch: " + std::to_string(epoch) + ", index: " + std::to_string(index) +
           ", data size: " + std::to_string(data.size());
  }
};

struct AppendEntriesRawMessage {
  MessageHeader header;

  int from = 0;
  uint64_t epoch = 0;
  uint64_t prev_term = 0;
  uint64_t prev_index = 0;
  uint64_t commit = 0;
  uint64_t group_id = 0;

  EntryRaw entry;

  std::string debug() {
    std::stringstream ss;
    ss << "from: " << from << ", " <<
    "epoch: " << epoch << ", " <<
    "prev_term: " << prev_term << "," <<
    "prev_index: " << prev_index << "," <<
    "commit: " << commit << "," <<
    "group_id: " << group_id << "," <<
    "entry index: " << entry.index << ", " <<
    "entry epoch: " << entry.epoch << ", " <<
    "entry data size: " << entry.data_size;

    return std::move(ss.str());
  }
};

// struct AppendEntriesMessage {
//   int from = 0;
//   uint64_t epoch = 0;

//   uint64_t prev_term = 0;
//   uint64_t prev_index = 0;
//   uint64_t commit = 0;
//   uint64_t group_id = 0;
//   Entry entry;

//   std::string debug() {
//     std::stringstream ss;
//     ss << "from: " << from << ", " <<
//     "epoch: " << epoch << ", " <<
//     "prev_term: " << prev_term << "," <<
//     "prev_index: " << prev_index << "," <<
//     "commit: " << commit << "," <<
//     "group_id: " << group_id << "," <<
//     "entry index: " << entry.index << ", " <<
//     "entry epoch: " << entry.epoch << ", " <<
//     "entry data size: " << entry.data.size();

//     return std::move(ss.str());
//   }
// };

struct AppendEntriesReplyMessage {
  MessageHeader header;

  int from;
  uint64_t epoch;
  bool success;
  uint64_t group_id;
  uint64_t index;
};

// struct ClientMessage {
//   // Guidance guidance;
//   uint64_t epoch;
//   uint64_t key_hash;
//   std::vector<uint8_t> op;  // contains full OperationRaw
// };

struct ClientRawMessage {
  MessageHeader header;

  uint64_t epoch;
  uint64_t key_hash;
  size_t data_size;

  uint8_t* get_op_buf() {
    uint8_t *op_buf = reinterpret_cast<uint8_t *>(this);
    return (op_buf + sizeof(ClientRawMessage));
  }

  OperationRaw& get_op() {
    uint8_t *op_buf = reinterpret_cast<uint8_t *>(this);
    op_buf += sizeof(ClientRawMessage);
    return *reinterpret_cast<OperationRaw *>(op_buf);
  }
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

  uint8_t* get_value_buf() {
    uint8_t *value_buf = reinterpret_cast<uint8_t *>(this);
    return (value_buf + sizeof(ClientReplyRawMessage));
  }
};

struct GuidanceMessage {
  MessageHeader header;
  int from;
  int votes;
  Guidance guide;
};

struct GossipMessage {
  MessageHeader header;
  int from;
  Guidance guide;
  uint64_t load;

  GossipMessage() {
    header.type = MessageType::MsgTypeGossip;
    header.size = sizeof(GossipMessage);
  }
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