
#ifndef __CORE_SMR_H__
#define __CORE_SMR_H__

#include <string.h>

#include <string>
#include <cstdint>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <functional>

#include <msgpack.hpp>

#include "guidance.h"
#include "message.h"

class SMRLog {
  std::vector<Entry> m_log;
  uint64_t m_commits = 0;
  Entry m_dummy_entry;

  SMRLog(SMRLog &l) = delete;

  uint64_t entry_pos(uint64_t e_index);
 public:
  SMRLog();

  uint64_t append(Entry &e);
  // uint64_t append(std::vector<uint8_t> &data, uint64_t term);
  uint64_t append(uint8_t *data, size_t data_size, uint64_t term);
  // truncate including t_idx
  void truncate(uint64_t t_idx);

  Entry& entry_at(uint64_t index);
  size_t get_tail_entries(std::vector<Entry> &dest, uint64_t start);
  std::vector<Entry> get_part_entries(uint64_t start, uint64_t ex_end);

  bool commit_to(uint64_t index);
  uint64_t curr_commit();

  Entry& last_entry();
  uint64_t last_index();

  std::string debug();
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

// struct SMRMessageCallback {
//   std::function<bool(GenericMessage &&msg)> notify_send_append_entry;
//   std::function<bool(GenericMessage &&msg)> notify_send_append_entry_reply;
//   std::function<bool(std::vector<Entry> entries)> notify_apply;
// };

template <typename T>
using message_cb_t = std::function<bool(T &msg, int to)>;

using apply_cb_t = std::function<bool(SMRLog &log, size_t start, size_t end)>;

using ae_cb_t = std::function<AppendEntriesRawMessage&(int id)>;

struct PeerStatus {
  uint64_t match;
  uint64_t next;
};

class SMR {
  SMRLog m_log;
  uint64_t m_gid;
  int m_me;
  uint64_t m_term;
  std::vector<int> &m_peers;
  EntryVoteRecord m_entry_votes;
  // SMRMessageCallback m_cb;
  std::unordered_map<int, PeerStatus> m_prs;

  message_cb_t<AppendEntriesRawMessage> notify_send_append_entry = nullptr;
  message_cb_t<AppendEntriesReplyMessage> notify_send_append_entry_reply = nullptr;
  apply_cb_t notify_apply = nullptr;

  ae_cb_t pre_alloc_ae = nullptr;
  // std::unordered_map<int, AppendEntriesMessage> m_prealloc_aes;

  // @return <is_safe, is_stale>
  std::tuple<bool, bool> check_safety(uint64_t prev_term, uint64_t prev_index);
  // void handle_stale_entries(AppendEntriesMessage &msg);
  void init();

public:
  SMR(int me, std::vector<int> &peers);
  // SMR(int me, std::vector<int> &peers, SMRMessageCallback cb);
  SMR(SMR &&smr);
  void set_cb(message_cb_t<AppendEntriesRawMessage> ae_cb,
              message_cb_t<AppendEntriesReplyMessage> aer_cb,
              apply_cb_t apply_cb);
  void set_pre_alloc_ae_cb(ae_cb_t ae_cb);
  void set_gid(uint64_t gid);
  void set_term(uint64_t term);

  // uint64_t handle_operation(ClientMessage &msg);
  uint64_t handle_operation(ClientRawMessage &msg);
  // void handle_append_entries(AppendEntriesMessage &msg);
  void handle_append_entries(AppendEntriesRawMessage &msg, AppendEntriesReplyMessage &reply);
  void handle_append_entries_reply(AppendEntriesReplyMessage &msg);

  void send_append_entries(int to);
  void send_append_entries_reply(AppendEntriesReplyMessage &reply, int to);

  // debug
  SMRLog& debug_get_log() {
    return m_log;
  }
};

#endif  //__CORE_SMR_H__