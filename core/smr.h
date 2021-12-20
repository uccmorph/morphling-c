
#ifndef __CORE_SMR_H__
#define __CORE_SMR_H__

#include <string.h>

#include <string>
#include <cstdint>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <functional>

#include "guidance.h"

using group_t = size_t;
struct Entry {
  // bool valid = true;
  uint64_t epoch = 0;
  uint64_t index = 0;
  // size_t data_size;
  std::vector<uint8_t> data;
  Entry() {}
  // Entry(bool valid): valid(valid) {}
  Entry(uint64_t _epoch, uint64_t _index): epoch(_epoch), index(_index) {}
  Entry(std::vector<uint8_t> &buf): data(buf.begin(), buf.end()) {}
  ~Entry() {
    data.clear();
  }
  // Entry(Entry &&e) : epoch(e.epoch), index(e.index), data_size(e.data_size) {
  //   data = std::move(e.data);
  // }
  // Entry(const Entry &e) : epoch(e.epoch), index(e.index), data_size(e.data_size) {
  //   data = std::move(e.data);
  // }

  std::string debug() {
    char res[128];
    snprintf(res, 127, "epoch: %zu, index: %zu, real size: %zu",
             epoch, index, data.size());
    return std::string(res);
  }

 private:
  // Entry(const Entry &e) = delete;
  Entry &operator=(const Entry &e) = delete;
};

class SMRLog {
  std::vector<Entry> m_log;
  uint64_t m_commits = 0;
  Entry m_dummy_entry;

  SMRLog(SMRLog &l) = delete;

  uint64_t entry_pos(uint64_t e_index);
 public:
  SMRLog();

  void show_all();
  uint64_t append(Entry &e, uint64_t epoch);
  uint64_t append(Entry &e);
  // truncate including t_idx
  void truncate(uint64_t t_idx);

  Entry& entry_at(uint64_t index);
  std::vector<Entry> get_tail_entries(uint64_t in);
  std::vector<Entry> get_part_entries(uint64_t start, uint64_t ex_end);

  bool commit_to(uint64_t index);
  uint64_t curr_commit();

  Entry& last_entry();
  uint64_t last_index();
};

struct AppendEntriesMessage {
  uint64_t epoch;
  uint64_t prev_term;
  uint64_t prev_index;
  uint64_t commit;
  group_t group_id;

  std::vector<Entry> entries;
};

struct AppenEntriesReplyMessage {
  bool success;
  uint64_t epoch;
  uint64_t index;
};

struct Operation {
  int op_type;
  std::vector<uint8_t> buf;
};

struct ClientProposalMessage {
  // guidance_t guidance;
  uint64_t epoch;
  uint64_t key_hash;
  // size_t data_size;
  std::vector<uint8_t> data;
};

struct ReplyClientMessage {
  uint64_t epoch;
  uint64_t key_hash;
};

struct GenericMessage {
  int type;
  int to;
  AppendEntriesMessage append_msg;
  AppenEntriesReplyMessage append_reply_msg;
  ClientProposalMessage client_msg;

  GenericMessage(AppendEntriesMessage &msg, int to)
      : type(1), to(to), append_msg(msg) {}
  GenericMessage(AppenEntriesReplyMessage &msg, int to)
      : type(2), to(to), append_reply_msg(msg) {}
  GenericMessage(ClientProposalMessage &msg, int to)
      : type(3), to(to), client_msg(msg) {}
};

class EntryVoteRecord {
  std::unordered_map<uint64_t, std::vector<bool>> m_records;
  std::unordered_map<uint64_t, bool> m_entry_complete;
  int m_quorum;
  int m_peer_num;

public:
  void init(int num);
  // return true when the first time gather enough quorum
  bool vote(int id, uint64_t index);
};

struct SMRMessageCallback {
  std::function<bool(GenericMessage &&msg)> notify_send_append_entry;
  std::function<bool(GenericMessage &&msg)> notify_send_append_entry_reply;
  std::function<bool(std::vector<Entry> entries)> notify_apply;
};

struct PeerStatus {
  uint64_t match;
  uint64_t next;
};

class SMR {
  SMRLog m_log;
  group_t m_gid;
  int m_me;
  std::vector<int> &m_peers;
  EntryVoteRecord m_entry_votes;
  SMRMessageCallback m_cb;
  std::unordered_map<int, PeerStatus> m_prs;

  // @return <is_safe, is_stale>
  std::tuple<bool, bool> check_safety(uint64_t prev_term, uint64_t prev_index);
  void handle_stale_entries(AppendEntriesMessage &msg);

public:
  SMR(int me, std::vector<int> &peers, SMRMessageCallback cb);

  void handle_operation(ClientProposalMessage &msg);
  void handle_append_entries(AppendEntriesMessage &msg, int from);
  void handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from);

  void send_append_entries(int to);
  void send_append_entries_reply(AppenEntriesReplyMessage &reply, int to);

  // debug
  SMRLog& debug_get_log() {
    return m_log;
  }
};

#endif  //__CORE_SMR_H__