
#ifndef __CORE_SMR_H__
#define __CORE_SMR_H__

#include <string.h>

#include <cstdint>
#include <tuple>
#include <vector>
#include <unordered_map>

#include "guidance.h"

using group_t = size_t;
struct Entry {
  bool valid = true;
  uint64_t epoch;
  uint64_t index;
  size_t data_size;
  uint8_t *data = nullptr;
  Entry() {}
  Entry(bool valid): valid(valid) {}
  Entry(size_t size, uint8_t *buf) {
    data_size = size;
    data = new uint8_t[size];
    memcpy(data, buf, size);
  }
  ~Entry() {
    if (data) {
      delete[] data;
      data = nullptr;
    }
  }
  Entry(Entry &&e) : epoch(e.epoch), index(e.index), data_size(e.data_size) {
    data = new uint8_t[data_size];
    memcpy(data, e.data, data_size);
  }

 private:
  Entry(const Entry &e) = delete;
  Entry &operator=(const Entry &e) = delete;
};

class SMRLog {
  std::vector<Entry> m_log;
  uint64_t commits;
  Entry invalid_entry;

  SMRLog(SMRLog &l) = delete;

 public:
  SMRLog();

  uint64_t append(Entry &e);
  // truncate including t_idx
  void truncate(uint64_t t_idx);

  Entry &entry_at(uint64_t index);
  void commit_to(uint64_t index);
  uint64_t curr_commit();

  Entry &last_entry();
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

struct ClientProposalMessage {
  // guidance_t guidance;
  uint64_t epoch;
  uint64_t key_hash;
  size_t data_size;
  uint8_t *data;
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

class SMR {
  SMRLog m_log;
  group_t m_gid;
  int m_me;
  std::vector<int> &m_peers;
  guidance_t &m_guide;
  EntryVoteRecord m_entry_votes;

  // @return <is_safe, is_stale>
  std::tuple<bool, bool> check_safety(uint64_t prev_term, uint64_t prev_index);
  void handle_stale_entries(AppendEntriesMessage &msg);

public:
  SMR(std::vector<int> &_m_peers, guidance_t &_m_guide);
  void handle_operation(ClientProposalMessage &msg);
  void handle_append_entries(AppendEntriesMessage &msg);
  void handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from);
  void send_append_entries(int to);
  void reply_client();
  bool is_valid_guidance(uint64_t epoch);
  void reply_guidance();
  void send_append_entries_reply(AppenEntriesReplyMessage &reply);

};

#endif  //__CORE_SMR_H__