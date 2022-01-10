#ifndef __CORE_MESSAGE_H__
#define __CORE_MESSAGE_H__

#include <cstdint>
#include <vector>

#include <msgpack.hpp>

#include "guidance.h"


struct Entry;

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

struct AppendEntriesMessage{
  int from;
  uint64_t epoch;

  uint64_t prev_term;
  uint64_t prev_index;
  uint64_t commit;
  uint64_t group_id;
  std::vector<Entry> entries;

  MSGPACK_DEFINE(from, epoch, prev_term, prev_index, commit, group_id, entries);
};

struct AppenEntriesReplyMessage{
  int from;
  uint64_t epoch;

  bool success;
  uint64_t group_id;
  uint64_t index;

  MSGPACK_DEFINE(from, epoch, success, group_id, index);
};

struct Operation {
  int op_type; // 0 for read, 1 for write
  uint64_t key_hash;
  std::vector<uint8_t> data;

  MSGPACK_DEFINE(op_type, key_hash, data);
};

struct ClientMessage{
  // Guidance guidance;
  uint64_t epoch;
  uint64_t key_hash;
  // size_t data_size;
  std::vector<uint8_t> op;


  MSGPACK_DEFINE(epoch, key_hash, op);
};

struct ClientReplyMessage{
  int from;
  bool success;
  Guidance guidance;
  uint64_t key_hash;
  std::vector<uint8_t> reply_data;

  MSGPACK_DEFINE(from, success, guidance, key_hash, reply_data);
};

struct GuidanceMessage{
  int from;
  int votes;
  Guidance guide;

  // MSGPACK_DEFINE(from, guide, votes);
};

struct GenericMessage {
  MessageType type;
  int to;
  AppendEntriesMessage append_msg;
  AppenEntriesReplyMessage append_reply_msg;
  ClientMessage client_msg;
  GuidanceMessage guidance_msg;

  GenericMessage(AppendEntriesMessage &msg, int to)
      : type(MsgTypeAppend), to(to), append_msg(msg) {}
  GenericMessage(AppenEntriesReplyMessage &msg, int to)
      : type(MsgTypeAppendReply), to(to), append_reply_msg(msg) {}
  GenericMessage(ClientMessage &msg, int to)
      : type(MsgTypeClient), to(to), client_msg(msg) {}
  GenericMessage(GuidanceMessage &msg, int to)
      : type(MsgTypeGuidance), to(to), guidance_msg(msg) {}
};


struct MessageHeader {
  uint32_t size:24;
  uint32_t type:8;
};

#endif // __CORE_MESSAGE_H__