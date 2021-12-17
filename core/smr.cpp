#include "smr.h"

#include <stdlib.h>

#include <algorithm>
#include <loguru.hpp>
#include <utility>

SMRLog::SMRLog() : invalid_entry(false) {}

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
  auto &entry_record = (*records_itr).second;
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

SMR::SMR(std::vector<int> &_peers, guidance_t &_guide)
    : m_peers(_peers), m_guide(_guide) {
  m_entry_votes.init( _peers.size());
}

void SMR::handle_operation(ClientProposalMessage &msg) {
  Entry e(msg.data_size, msg.data);
  m_log.append(e);
  for (auto rid : m_peers) {
    if (rid == m_me) {
      send_append_entries(rid);
    }
  }
}

void SMR::handle_append_entries(AppendEntriesMessage &msg) {
  AppenEntriesReplyMessage reply;
  reply.epoch = m_guide.epoch;
  reply.index = msg.prev_index;
  reply.success = false;

  auto safety_res = check_safety(msg.prev_term, msg.prev_index);
  if (!std::get<0>(safety_res)) {
    LOG_F(ERROR, "This AppendEntriesMessage is not safe");
  } else {
    if (std::get<1>(safety_res)) {
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
      m_log.truncate((*itr).index);
      std::vector<Entry> sub_entries(itr, msg.entries.end());
      for (auto &e : sub_entries) {
        m_log.append(e);
      }
      break;
    }
  }
}

void SMR::handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

  if (!msg.success) {
    // tbd. nextIndex and matchIndex may not be realistic.
    return;
  }

  if (m_entry_votes.vote(from, msg.index)) {
    m_log.commit_to(msg.index);
  }
}