
#include <loguru.hpp>
#include "morphling.h"

Morphling::Morphling(int id, std::vector<int> &peers)
    : m_me(id), m_peers(peers) {
  init_local_guidance();
}

Morphling::~Morphling() {}

void Morphling::init_local_guidance(int key_space) {
  m_guide.epoch = 1;
  if (m_peers.size() != HARD_CODE_REPLICAS) {
    LOG_F(ERROR, "peers size %zu should be equal to %d", m_peers.size(),
          HARD_CODE_REPLICAS);
    exit(-1);
  }
  m_guide.alive_num = m_peers.size();
  m_guide.cluster_size = m_peers.size();
  for (int i = 0; i < m_guide.cluster_size; i++) {
    m_guide.cluster[i].alive = 0x1;
    m_guide.cluster[i].start_key_pos = i * key_space / m_guide.cluster_size;
    m_guide.cluster[i].end_key_pos = (i + 1) * key_space / m_guide.cluster_size;
  }
}

int Morphling::calc_key_pos(uint64_t key_hash) {
  return key_hash & DEFAULT_KEY_MASK >> DEFAULT_MASK_OFFSET;
}

void Morphling::handle_operation(ClientProposalMessage &msg) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

}

void Morphling::handle_append_entries(AppendEntriesMessage &msg) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

}


void Morphling::handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

}