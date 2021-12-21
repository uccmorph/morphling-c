#ifndef __CORE_MESSAGE_H__
#define __CORE_MESSAGE_H__

#include <cstdint>
#include <vector>

#include <msgpack.hpp>

#include "guidance.h"


struct Entry;

struct AppendEntriesMessage {
  uint64_t epoch;

  uint64_t prev_term;
  uint64_t prev_index;
  uint64_t commit;
  uint64_t group_id;
  std::vector<Entry> entries;

  MSGPACK_DEFINE(epoch, prev_term, prev_index, commit, group_id, entries);
};

struct AppenEntriesReplyMessage {
  uint64_t epoch;
  
  bool success;
  uint64_t group_id;
  uint64_t index;
};

struct Operation {
  int op_type;
  uint64_t key_hash;
  std::vector<uint8_t> buf;
};

struct ClientMessage {
  // guidance_t guidance;
  uint64_t epoch;
  uint64_t key_hash;
  // size_t data_size;
  std::vector<uint8_t> data;
};

struct GuidanceMessage {
  guidance_t guide;

  MSGPACK_DEFINE(guide);
};

struct GenericMessage {
  int type;
  int to;
  AppendEntriesMessage append_msg;
  AppenEntriesReplyMessage append_reply_msg;
  ClientMessage client_msg;
  GuidanceMessage guidance_msg;

  GenericMessage(AppendEntriesMessage &msg, int to)
      : type(1), to(to), append_msg(msg) {}
  GenericMessage(AppenEntriesReplyMessage &msg, int to)
      : type(2), to(to), append_reply_msg(msg) {}
  GenericMessage(ClientMessage &msg, int to)
      : type(3), to(to), client_msg(msg) {}
  GenericMessage(GuidanceMessage &msg, int to)
      : type(4), to(to), guidance_msg(msg) {}
};

#endif // __CORE_MESSAGE_H__