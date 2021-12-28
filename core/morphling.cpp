
#include <loguru.hpp>
#include "morphling.h"

Morphling::Morphling(int id, std::vector<int> &peers)
    : m_me(id), m_peers(peers) {
  Morphling::init_local_guidance();
  SMRMessageCallback cb{
      .notify_send_append_entry = [this](GenericMessage &&msg) -> bool {
        this->m_next_msgs.push_back(msg);
        return true;
      },
      .notify_send_append_entry_reply = [this](GenericMessage &&msg) -> bool {
        this->m_next_msgs.push_back(msg);
        return true;
      },
      .notify_apply = [this](std::vector<Entry> entries) -> bool {
        for (auto &item : entries) {
          this->m_apply_entries.push_back(item);
        }
        return true;
      }};
  for (int i = 0; i < DEFAULT_KEY_SPACE; i++) {
    // SMR smr(id, m_peers);
    auto &smr = m_smrs.emplace_back(id, m_peers);
    smr.set_gid(i);
    smr.set_cb(cb);
  }
}

Morphling::~Morphling() {}

#if 0
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
#endif

void Morphling::init_local_guidance(int key_space) {
  m_guide.epoch = 1;
  if (m_peers.size() != HARD_CODE_REPLICAS) {
    LOG_F(ERROR, "peers size %zu should be equal to %d", m_peers.size(),
          HARD_CODE_REPLICAS);
    exit(-1);
  }
  uint32_t alive = m_peers.size();
  uint32_t cluster_size = m_peers.size();
  m_guide.status = 0x0;

  // assume never exceed 8 bits
  m_guide.status |= alive;
  m_guide.status |= (cluster_size << guidance_offset_cluster);
  for (int i = 0; i < cluster_size; i++) {
    m_guide.cluster[i].status = 0;

    uint32_t start_key_pos = uint32_t(i * key_space) / cluster_size;
    uint32_t end_key_pos = uint32_t((i + 1) * key_space) / cluster_size;
    m_guide.cluster[i].status |= (start_key_pos << 0);
    m_guide.cluster[i].status |= (end_key_pos << node_status_offset_end);
    m_guide.cluster[i].status |= (0x1 << node_status_offset_flags);
  }
}

int Morphling::calc_key_pos(uint64_t key_hash) {
  return key_hash & DEFAULT_KEY_MASK >> DEFAULT_MASK_OFFSET;
}

bool Morphling::is_valid_guidance(uint64_t epoch) {
  if (m_guide.epoch != epoch) {
    return false;
  }
  return true;
}

SMR& Morphling::find_target_smr(uint64_t key_hash) {
  auto group_id = calc_key_pos(key_hash);
  auto &smr = m_smrs[group_id];
  return smr;
}

std::unique_ptr<Operation> Morphling::parse_operation(std::vector<uint8_t> data) {
  std::string data_str(data.begin(), data.end());
  auto oh = msgpack::unpack(data_str.data(), data_str.size());
  auto o = oh.get();

  std::unique_ptr<Operation> op = std::make_unique<Operation>();
  o.convert(*op.get());

  return op;
}

void Morphling::handle_operation(ClientMessage &msg, std::unique_ptr<Transport> &&trans) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

  auto op = parse_operation(msg.op);
  if (op.get()->op_type == 0) {
    // send echo msg
    std::string echo_str("hello world!");
    trans->send((uint8_t *)echo_str.c_str(), echo_str.size());
    return;
  }

  auto new_idx = Morphling::find_target_smr(msg.key_hash).handle_operation(msg);
  m_client_pendings[new_idx] = std::move(trans);
  prepare_msgs();
}

void Morphling::handle_append_entries(AppendEntriesMessage &msg, int from) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

  m_smrs[msg.group_id].handle_append_entries(msg, from);
  prepare_msgs();
}


void Morphling::handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from) {
  if (!is_valid_guidance(msg.epoch)) {
    reply_guidance();
    return;
  }

  m_smrs[msg.group_id].handle_append_entries_reply(msg, from);
  maybe_apply();
  prepare_msgs();

}

void Morphling::reply_guidance() {}

bool Morphling::prepare_msgs() {
  if (m_next_msgs.size() == 0) {
    return false;
  }
  for (auto &msg : m_next_msgs) {
    switch (msg.type) {
    case MsgTypeAppend:
      msg.append_msg.from = m_me;
      msg.append_msg.epoch = m_guide.epoch;
      break;
    case MsgTypeAppendReply:
      msg.append_reply_msg.from = m_me;
      msg.append_reply_msg.epoch = m_guide.epoch;
      break;
    case MsgTypeClient:
      msg.client_msg.from = m_me;
      msg.client_msg.epoch = m_guide.epoch;
      break;
    }
  }
}

bool Morphling::maybe_apply() {
  for (auto &e : m_apply_entries) {
    LOG_F(3, "apply entry: %s", e.debug().c_str());
    std::string echo_str("hello world!");
    m_client_pendings[e.index]->send((uint8_t *)echo_str.c_str(), echo_str.size());
  }
  m_apply_entries.clear();
}

void Morphling::bcast_msgs(std::unordered_map<int, std::unique_ptr<Transport>> &peers_trans) {
  if (m_next_msgs.size() == 0) {
    return;
  }
  for (auto &msg : m_next_msgs) {
    auto &trans = peers_trans[msg.to];
    // todo:

    switch (msg.type) {
    case MsgTypeAppend:
      trans->send(msg.append_msg);
      break;
    case MsgTypeAppendReply:
      trans->send(msg.append_reply_msg);
      break;
    case MsgTypeGuidance:
      trans->send(msg.guidance_msg);
      break;
    }
  }
  m_next_msgs.clear();
}

guidance_t& Morphling::get_guidance() {
  return m_guide;
}

std::vector<GenericMessage>& Morphling::debug_next_msgs() {
  return m_next_msgs;
}

std::vector<Entry>& Morphling::debug_apply_entries() {
  return m_apply_entries;
}