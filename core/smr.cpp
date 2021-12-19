#include "smr.h"

#include <stdlib.h>

#include <algorithm>
#include <loguru.hpp>
#include <stdexcept>
#include <utility>

SMRLog::SMRLog() : m_dummy_entry(0, 0) { m_log.reserve(10000); }

uint64_t SMRLog::entry_pos(uint64_t e_index) {
  if (e_index < m_dummy_entry.index) {
    return 0;
  }
  return e_index - m_dummy_entry.index - 1;
}

void SMRLog::show_all() {
  LOG_F(INFO, "log size: %zu, dummy entry (epoch %zu, index %zu), commit: %zu",
        m_log.size(), m_dummy_entry.epoch, m_dummy_entry.index, m_commits);
  for (auto itr = m_log.begin(); itr != m_log.end(); itr++) {
    LOG_F(INFO, "itr %ld, %s", itr - m_log.begin(), (*itr).debug().c_str());
  }
}

uint64_t SMRLog::append(Entry &e) {
  if (m_log.size() == 0) {
    m_log.emplace_back(e.data);
    auto &new_e = m_log.back();
    new_e.index = 1;
    return 1;
  }
  auto &last_e = m_log.back();
  m_log.emplace_back(e.data);
  auto &new_e = m_log.back();
  new_e.index = last_e.index + 1;

  return new_e.index;
}

void SMRLog::truncate(uint64_t t_idx) {
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
    char err_msg[128];
    snprintf(err_msg, 128,
             "visit entry index %zu at log position %zu, excced log length %zu",
             index, real_index, m_log.size());
    throw std::invalid_argument(err_msg);
  }
  return m_log[real_index];
}

bool SMRLog::commit_to(uint64_t index) {
  if (index > last_index()) {
    char err_msg[128];
    snprintf(err_msg, 128, "want to commit entry %zu excced last log %zu", index, last_index());
    throw std::invalid_argument(err_msg);
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
  LOG_F(INFO, "init entry vote");
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
  LOG_F(INFO, "size of entry_record (index %d): %zu", id, entry_record.size());
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

SMR::SMR(int me, std::vector<int> &peers, std::vector<GenericMessage> &cb_msgs)
    : m_me(me), m_peers(peers), m_cb_msgs(cb_msgs) {
  m_entry_votes.init(peers.size());
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
  } else if (prev_index == last_entry.epoch) {
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

void SMR::handle_operation(ClientProposalMessage &msg) {
  Entry e(msg.data);
  m_log.append(e);
  for (auto rid : m_peers) {
    if (rid != m_me) {
      send_append_entries(rid);
    }
  }
}

void SMR::handle_append_entries(AppendEntriesMessage &msg) {
  AppenEntriesReplyMessage reply;
  reply.index = msg.prev_index;
  reply.success = false;

  auto [safe, stale] = check_safety(msg.prev_term, msg.prev_index);
  if (!safe) {
    LOG_F(ERROR, "This AppendEntriesMessage is not safe");
  } else {
    if (stale) {
      handle_stale_entries(msg);
    } else {
      for (auto &e : msg.entries) {
        m_log.append(e);
      }
    }
    if (msg.commit > m_log.curr_commit()) {
      uint64_t new_commit = std::min(msg.commit, m_log.last_index());
      m_log.commit_to(new_commit);
    }
  }
  reply.index = m_log.last_index();
  send_append_entries_reply(reply);
}

void SMR::handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from) {

  if (!msg.success) {
    // tbd. nextIndex and matchIndex may not be realistic.
    return;
  }

  if (m_entry_votes.vote(from, msg.index)) {
    m_log.commit_to(msg.index);
  }
}

void SMR::send_append_entries(int to) {

}

void SMR::reply_client() {}

void SMR::reply_guidance() {}

void SMR::send_append_entries_reply(AppenEntriesReplyMessage &reply) {}
