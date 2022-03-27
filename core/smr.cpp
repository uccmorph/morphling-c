#include "smr.h"

#include <stdlib.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "loguru.hpp"

SMRLog::SMRLog() {
  m_dummy_entry.index = 0;
  m_dummy_entry.epoch = 0;
  m_log.reserve(1000000);
}

uint64_t SMRLog::entry_pos(uint64_t e_index) {
  if (e_index == 0) {
    ABORT_F("can not find entry index 0 in log");
  }
  if (e_index < m_dummy_entry.index) {
    return 0;
  }
  return e_index - m_dummy_entry.index - 1;
}

std::string SMRLog::debug() {
  std::stringstream ss;
  ss << "log size: " << m_log.size() << ", dummy entry (epoch " << m_dummy_entry.epoch <<
  ", index " << m_dummy_entry.index << "), commit: " << m_commits << "\n";
  for (size_t i = 0; i < m_log.size(); i++) {
    ss << "entry: " << i << ", " << m_log[i].debug() << "\n";
  }

  return std::move(ss.str());
}

uint64_t SMRLog::append(Entry &e) { return SMRLog::append(e.data.data(), e.data.size(), e.epoch); }

// uint64_t SMRLog::append(std::vector<uint8_t> &data, uint64_t epoch) {
//     if (m_log.size() == 0) {
//     auto &new_e = m_log.emplace_back(data);
//     new_e.index = 1;
//     new_e.epoch = epoch;
//     return 1;
//   }
//   // caveat: After emplace new data, vector may change its size, resulting a
//   // stale last_e reference see
//   // https://en.cppreference.com/w/cpp/container/vector/emplace_back
//   auto &last_e = m_log.back();
//   auto &new_e = m_log.emplace_back(data);
//   new_e.index = last_e.index + 1;
//   new_e.epoch = epoch;

//   return new_e.index;
// }

uint64_t SMRLog::append(uint8_t *data, size_t data_size, uint64_t epoch) {
  auto &new_e = m_log.emplace_back(data, data_size);
  auto new_pos = m_log.size() - 1;
  new_e.index = m_dummy_entry.index + 1 + new_pos;
  new_e.epoch = epoch;

  return new_e.index;
}

void SMRLog::truncate(uint64_t t_idx) {
  LOG_F(WARNING, "truncate entry %zu", t_idx);
  auto target_last_itr = m_log.begin() + entry_pos(t_idx);
  while (target_last_itr < m_log.end()) {
    m_log.pop_back();
  }
}

Entry &SMRLog::entry_at(uint64_t index) {
  if (index == 0 || index <= m_dummy_entry.index) {
    return m_dummy_entry;
  }
  uint64_t real_index = entry_pos(index);
  if (real_index >= m_log.size()) {
    ABORT_F("visit entry index %zu at log position %zu, excced log length %zu",
            index, real_index, m_log.size());
  }
  return m_log[real_index];
}

// remove all old contents in dest
size_t SMRLog::get_tail_entries(std::vector<Entry> &dest, uint64_t start) {
  auto start_pos = entry_pos(start);
  dest.assign(m_log.begin() + start_pos, m_log.end());

  return m_log.end() - m_log.begin() - start_pos;
}

// get entries including start and end
std::vector<Entry> SMRLog::get_part_entries(uint64_t start, uint64_t end) {
  if (end < start) {
    ABORT_F("retrieve entries frome %zu to %zu, out of order", start, end);
  }
  auto start_pos = entry_pos(start);
  auto ex_end_pos = entry_pos(end) + 1;
  std::vector<Entry> res(m_log.begin() + start_pos, m_log.begin() + ex_end_pos);
  return res;
}

bool SMRLog::commit_to(uint64_t index) {
  if (index > last_index()) {
    ABORT_F("want to commit entry %zu excced last log %zu", index,
            last_index());
  }

  if (index > m_commits) {
    m_commits = index;
    return true;
  }
  return false;
}

uint64_t SMRLog::curr_commit() { return m_commits; }

Entry &SMRLog::last_entry() {
  if (m_log.size() == 0) {
    return m_dummy_entry;
  }
  auto itr = m_log.end() - 1;
  return *itr;
}

uint64_t SMRLog::last_index() { return last_entry().index; }

void EntryVoteRecord::init(int num) {
  m_peer_num = num;
  m_quorum = num / 2 + 1;
}

bool EntryVoteRecord::vote(int id, uint64_t index) {
  auto has_complete = m_entry_complete.find(index);
  if (has_complete != m_entry_complete.end()) {
    return false;
  }

  auto records_itr = m_records.find(index);
  if (records_itr == m_records.end()) {
    // m_records.emplace(index, std::vector<bool>(m_peer_num, false));
    m_records[index] = std::vector<bool>(m_peer_num, false);
  }
  auto &entry_record = m_records.at(index);
  entry_record[id] = true;

  int agree_count = 0;
  for (auto is_agree : entry_record) {
    if (is_agree) {
      agree_count += 1;
    }
  }

  if (agree_count == m_quorum) {
    m_entry_complete[index] = true;
    return true;
  }
  return false;
}

// ---------------- State Machine Replication ------------------

SMR::SMR(int me, std::vector<int> &peers) : m_me(me), m_peers(peers) {
  SMR::init();
}

// SMR::SMR(int me, std::vector<int> &peers, SMRMessageCallback cb)
//     : m_me(me), m_peers(peers), m_cb(cb) {
//   SMR::init();
// }

SMR::SMR(SMR &&smr) : m_peers(smr.m_peers) {
  LOG_F(FATAL, "shouldn't calling %s", __PRETTY_FUNCTION__);
  SMR::init();
}

void SMR::set_cb(message_cb_t<AppendEntriesRawMessage> ae_cb,
                 message_cb_t<AppendEntriesReplyMessage> aer_cb,
                 apply_cb_t apply_cb) {
  SMR::notify_send_append_entry = ae_cb;
  SMR::notify_send_append_entry_reply = aer_cb;
  SMR::notify_apply = apply_cb;
}

void SMR::set_pre_alloc_ae_cb(ae_cb_t ae_cb) {
  pre_alloc_ae = ae_cb;
}

void SMR::set_gid(uint64_t gid) { m_gid = gid; }

void SMR::set_term(uint64_t epoch) { m_term = epoch; }

void SMR::init() {
  m_entry_votes.init(m_peers.size());
  for (auto id : m_peers) {
    m_prs[id] = PeerStatus{0, 0};
    // m_prealloc_aes[id] = AppendEntriesMessage();
    // m_prealloc_aes[id].entry.data.reserve(4096);
  }
}

// return: is_safe, is_stale
std::tuple<bool, bool> SMR::check_safety(uint64_t prev_term,
                                         uint64_t prev_index) {
  Entry &last_entry = m_log.last_entry();
  if (prev_index < last_entry.index) {
    Entry &te = m_log.entry_at(prev_index);
    if (prev_term != te.epoch) {
      return std::make_tuple(false, false);
    } else {
      return std::make_tuple(true, true);
    }
  } else if (prev_index == last_entry.index) {
    if (prev_term != last_entry.epoch) {
      return std::make_tuple(false, false);
    } else {
      return std::make_tuple(true, false);
    }
  }
  return std::make_tuple(false, false);
}

// uint64_t SMR::handle_operation(ClientMessage &msg) {
//   auto last_idx = m_log.append(msg.op.data(), msg.op.size(), msg.epoch);
//   m_entry_votes.vote(m_me, last_idx);
//   m_prs[m_me].match = last_idx;
//   m_prs[m_me].next = last_idx + 1;
//   for (auto rid : m_peers) {
//     if (rid != m_me) {
//       m_prs[rid].next = last_idx;
//       send_append_entries(rid);
//     }
//   }
//   return last_idx;
// }

uint64_t SMR::handle_operation(ClientRawMessage &msg) {
  uint8_t *op_buf = msg.get_op_buf();
  auto last_idx = m_log.append(op_buf, msg.data_size, m_term);

  m_entry_votes.vote(m_me, last_idx);
  m_prs[m_me].match = last_idx;
  m_prs[m_me].next = last_idx + 1;
  for (auto rid : m_peers) {
    if (rid != m_me) {
      m_prs[rid].next = last_idx;
      send_append_entries(rid);
    }
  }
  return last_idx;
}

// void SMR::handle_append_entries(AppendEntriesMessage &msg) {
//   AppendEntriesReplyMessage reply;

//   auto [safe, stale] = check_safety(msg.prev_term, msg.prev_index);
//   if (!safe) {
//     reply.success = false;
//     LOG_F(ERROR, "This AppendEntriesMessage is not safe");
//   } else {
//     reply.success = true;
//     if (stale) {
//       // just ignore, since only one entry
//     } else {
//       m_log.append(msg.entry);
//     }
//     if (msg.commit > m_log.curr_commit()) {
//       uint64_t new_commit = std::min(msg.commit, m_log.last_index());
//       LOG_F(3, "passive commit to %zu\n", new_commit);
//       m_log.commit_to(new_commit);
//     }
//   }
//   reply.index = m_log.last_index();
//   send_append_entries_reply(reply, msg.from);
// }

void SMR::handle_append_entries(AppendEntriesRawMessage &msg, AppendEntriesReplyMessage &reply) {
  auto [safe, stale] = check_safety(msg.prev_term, msg.prev_index);
  if (!safe) {
    reply.success = false;
    LOG_F(ERROR, "This AppendEntriesMessage is not safe");
  } else {
    reply.success = true;
    if (stale) {
      // just ignore, since only one entry
    } else {
      m_log.append(msg.entry.get_op_buf(), msg.entry.data_size, msg.entry.epoch);
    }
    if (msg.commit > m_log.curr_commit()) {
      uint64_t new_commit = std::min(msg.commit, m_log.last_index());
      LOG_F(3, "passive commit to %zu\n", new_commit);
      m_log.commit_to(new_commit);
    }
  }
  reply.index = m_log.last_index();
  send_append_entries_reply(reply, msg.from);
}

void SMR::handle_append_entries_reply(AppendEntriesReplyMessage &msg) {
  if (!msg.success) {
    // tbd. nextIndex and matchIndex may not be realistic.
    LOG_F(FATAL, "current not supprt recovery on append entry failure");
    return;
  }
  LOG_F(3, "peer %d vote for entry %zu", msg.from, msg.index);
  if (m_entry_votes.vote(msg.from, msg.index)) {
    uint64_t old_commit = m_log.curr_commit();
    bool ok = m_log.commit_to(msg.index);
    if (ok) {
      LOG_F(3, "entry %zu is newly committed", msg.index);
      // auto apply_entries = m_log.get_part_entries(old_commit+1, msg.index);
      if (notify_apply != nullptr) {
        notify_apply(m_log, old_commit + 1, msg.index);
      }
    }
  }
}

void SMR::send_append_entries(int to) {
  if (notify_send_append_entry == nullptr) {
    return;
  }
  auto prev_idx = m_prs[to].next - 1;
  auto prev_term = m_log.entry_at(prev_idx).epoch;

  if (pre_alloc_ae == nullptr) {
    LOG_F(FATAL, "don't have proper pre-alloc append entry message");
  }
  AppendEntriesRawMessage &append_msg = pre_alloc_ae(to);
  append_msg.from = m_me;
  append_msg.epoch = m_term;
  append_msg.prev_term = prev_term;
  append_msg.prev_index = prev_idx;
  append_msg.commit = m_log.curr_commit();
  append_msg.group_id = m_gid;

  Entry &e = m_log.entry_at(m_prs[to].next);
  append_msg.entry.epoch = e.epoch;
  append_msg.entry.index = e.index;
  append_msg.entry.data_size = e.data.size();
  std::copy(e.data.begin(), e.data.end(), append_msg.entry.get_op_buf());

  append_msg.header.type = MessageType::MsgTypeAppend;
  append_msg.header.size = sizeof(AppendEntriesRawMessage) + e.data.size();

  notify_send_append_entry(append_msg, to);
}

void SMR::send_append_entries_reply(AppendEntriesReplyMessage &reply, int to) {
  if (notify_send_append_entry_reply == nullptr) {
    return;
  }
  reply.header.type = MessageType::MsgTypeAppendReply;
  reply.header.size = sizeof(AppendEntriesReplyMessage);
  reply.group_id = m_gid;
  reply.from = m_me;
  reply.epoch = m_term;

  notify_send_append_entry_reply(reply, to);
}
