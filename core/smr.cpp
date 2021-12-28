#include "smr.h"

#include <stdlib.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include "loguru.hpp"

SMRLog::SMRLog() : m_dummy_entry(0, 0) { m_log.reserve(1000000); }

uint64_t SMRLog::entry_pos(uint64_t e_index) {
  if (e_index == 0) {
    ABORT_F("can not find entry index 0 in log");
  }
  if (e_index < m_dummy_entry.index) {
    return 0;
  }
  return e_index - m_dummy_entry.index - 1;
}

void SMRLog::show_all() {
  LOG_F(3, "log size: %zu, dummy entry (epoch %zu, index %zu), commit: %zu",
        m_log.size(), m_dummy_entry.epoch, m_dummy_entry.index, m_commits);
  for (auto itr = m_log.begin(); itr != m_log.end(); itr++) {
    LOG_F(3, "itr %ld, %s", itr - m_log.begin(), (*itr).debug().c_str());
  }
}

uint64_t SMRLog::append(Entry &e, uint64_t epoch) {
  if (m_log.size() == 0) {
    auto &new_e = m_log.emplace_back(e.data);
    new_e.index = 1;
    new_e.epoch = epoch;
    return 1;
  }
  // caveat: After emplace new data, vector may change its size, resulting a stale last_e reference
  // see https://en.cppreference.com/w/cpp/container/vector/emplace_back
  auto &last_e = m_log.back();
  auto &new_e = m_log.emplace_back(e.data);
  new_e.index = last_e.index + 1;
  new_e.epoch = epoch;

  return new_e.index;
}

uint64_t SMRLog::append(Entry &e) {
  return SMRLog::append(e, e.epoch);
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

std::vector<Entry> SMRLog::get_tail_entries(uint64_t in) {
  return SMRLog::get_part_entries(in, last_index());
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
    ABORT_F("want to commit entry %zu excced last log %zu", index, last_index());
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
    m_records.emplace(index, std::vector<bool>(m_peer_num, false));
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

SMR::SMR(int me, std::vector<int> &peers): m_me(me), m_peers(peers) {
  SMR::init();
}

SMR::SMR(int me, std::vector<int> &peers, SMRMessageCallback cb)
    : m_me(me), m_peers(peers), m_cb(cb) {
  SMR::init();
}

SMR::SMR(SMR &&smr): m_peers(smr.m_peers) {

}

void SMR::set_cb(SMRMessageCallback cb) {
  m_cb = cb;
}

void SMR::set_gid(uint64_t gid) {
  m_gid = gid;
}

void SMR::init() {
  m_entry_votes.init(m_peers.size());
  for (auto id : m_peers) {
    m_prs[id] = PeerStatus{0, 0};
  }
}

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

void SMR::handle_stale_entries(AppendEntriesMessage &msg) {
  uint64_t last_index = m_log.last_index();
  for (auto itr = msg.entries.begin(); itr != msg.entries.end(); itr++) {
    Entry &e = m_log.entry_at((*itr).index);

    // truncate when sub entries mismatch
    if ((*itr).index > last_index || e.epoch != (*itr).epoch) {
      Entry &e = *itr;
      m_log.truncate(e.index);
      for (; itr != msg.entries.end(); itr++) {
        m_log.append(e);
      }
      break;
    }
  }
}

uint64_t SMR::handle_operation(ClientMessage &msg) {
  Entry e(msg.op);
  auto last_idx = m_log.append(e, msg.epoch);
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

void SMR::handle_append_entries(AppendEntriesMessage &msg, int from) {
  AppenEntriesReplyMessage reply;

  auto [safe, stale] = check_safety(msg.prev_term, msg.prev_index);
  if (!safe) {
    reply.success = false;
    LOG_F(ERROR, "This AppendEntriesMessage is not safe");
  } else {
    reply.success = true;
    if (stale) {
      handle_stale_entries(msg);
    } else {
      for (auto &e : msg.entries) {
        m_log.append(e);
      }
    }
    if (msg.commit > m_log.curr_commit()) {
      uint64_t new_commit = std::min(msg.commit, m_log.last_index());
      LOG_F(3, "passive commit to %zu\n", new_commit);
      m_log.commit_to(new_commit);
    }
  }
  reply.index = m_log.last_index();
  send_append_entries_reply(reply, from);
}

void SMR::handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from) {

  if (!msg.success) {
    // tbd. nextIndex and matchIndex may not be realistic.
    return;
  }
  LOG_F(3, "peer %d vote for entry %zu", from, msg.index);
  if (m_entry_votes.vote(from, msg.index)) {
    uint64_t old_commit = m_log.curr_commit();
    bool ok = m_log.commit_to(msg.index);
    if (ok) {
      LOG_F(3, "entry %zu is newly committed", msg.index);
      auto apply_entries = m_log.get_part_entries(old_commit+1, msg.index);
      m_cb.notify_apply(apply_entries);
    }
  }
}

void SMR::send_append_entries(int to) {
  auto prev_idx = m_prs[to].next - 1;
  auto prev_term = m_log.entry_at(prev_idx).epoch;
  AppendEntriesMessage append_msg {
    .prev_term = prev_term,
    .prev_index = prev_idx,
    .commit = m_log.curr_commit(),
    .group_id = m_gid,
    .entries = m_log.get_tail_entries(m_prs[to].next),
  };
  m_cb.notify_send_append_entry(GenericMessage(append_msg, to));
}

void SMR::send_append_entries_reply(AppenEntriesReplyMessage &reply, int to) {
  reply.group_id = m_gid;
  m_cb.notify_send_append_entry_reply(GenericMessage(reply, to));
}

