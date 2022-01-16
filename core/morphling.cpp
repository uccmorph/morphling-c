
#include <loguru.hpp>

#include "morphling.h"

int calc_key_pos(uint64_t key_hash) {
  return (key_hash & DEFAULT_KEY_MASK) >> DEFAULT_MASK_OFFSET;
}

Morphling::Morphling(int id, std::vector<int> &peers)
    : m_me(id), m_peers(peers) {
  Morphling::init_local_guidance();
  m_storage.reserve(1000);

  message_cb_t<AppendEntriesMessage> ae_cb = [this](AppendEntriesMessage &msg, int to) -> bool {
    auto &trans = get_peer_trans(to);
    trans->send(msg);
    return true;
  };
  message_cb_t<AppendEntriesReplyMessage> aer_cb = [this](AppendEntriesReplyMessage &msg, int to) -> bool {
    auto &trans = get_peer_trans(to);
    trans->send(msg);
    return true;
  };
  apply_cb_t apply_cb = [this](SMRLog &log, size_t start, size_t end) -> bool {
    for (size_t idx = start; idx <= end; idx++) {
      Entry &e = log.entry_at(idx);
      auto op = Morphling::parse_operation(e.data);
      uint8_t *reply_buf;
      size_t reply_buf_size = 0;
      if (op->op_type == 0) {
        auto &value = Morphling::m_storage[op->key_hash];
        reply_buf_size = sizeof(ClientReplyRawMessage) + value.size();
        reply_buf = new uint8_t[reply_buf_size];

        size_t data_offset = sizeof(ClientReplyRawMessage);
        std::copy(value.begin(), value.end(), reply_buf + data_offset);
      } else {
        reply_buf = new uint8_t[reply_buf_size];
        reply_buf_size = sizeof(ClientReplyRawMessage);

        m_storage[op->key_hash] = op->data;
      }
      ClientReplyRawMessage *reply = reinterpret_cast<ClientReplyRawMessage *>(reply_buf);
      reply->from = Morphling::m_me;
      reply->success = true;
      memcpy(&reply->guidance, &(Morphling::m_guide), sizeof(Guidance));
      reply->key_hash = op->key_hash;

      auto &trans = Morphling::m_client_pendings[idx];
      trans->send(reply_buf, reply_buf_size);
    }
    return true;
  };
  // alloc enough space, so m_smrs won't automatically grow, and won't use move constructor
  m_smrs.reserve(DEFAULT_KEY_SPACE);
  for (int i = 0; i < DEFAULT_KEY_SPACE; i++) {
    auto &smr = m_smrs.emplace_back(id, m_peers);
    smr.set_gid(i);
    smr.set_term(m_guide.term);
    smr.set_cb(ae_cb, aer_cb, apply_cb);
  }
}

Morphling::~Morphling() {}

void Morphling::init_local_guidance(int key_space) {
  m_guide.term = 1;
  if (m_peers.size() != ReplicaNumbers) {
    LOG_F(ERROR, "peers size %zu should be equal to %d", m_peers.size(),
          ReplicaNumbers);
    exit(-1);
  }

  int cluster_size = m_peers.size();

  m_guide.cluster_size = cluster_size;
  m_guide.alive_num = cluster_size;

  for (int i = 0; i < cluster_size; i++) {
    uint32_t start_key_pos = uint32_t(i * key_space) / cluster_size;
    uint32_t end_key_pos = uint32_t((i + 1) * key_space) / cluster_size - 1;
    m_guide.cluster[i].start_pos = start_key_pos;
    m_guide.cluster[i].end_pos = end_key_pos;
    m_guide.cluster[i].alive = 1;
  }
}

bool Morphling::is_valid_guidance(uint64_t term) {
  if (m_guide.term != term) {
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
  std::unique_ptr<Operation> op = std::make_unique<Operation>();
  OperationRaw *op_raw = reinterpret_cast<OperationRaw *>(data.data());
  op->op_type = op_raw->op_type;
  op->key_hash = op_raw->key_hash;
  size_t data_offset = sizeof(OperationRaw);
  op->data.assign(data.begin() + data_offset, data.end());

  return op;
}

void Morphling::handle_operation(ClientMessage &msg, TransPtr &trans) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(trans);
    return;
  }

  auto op = parse_operation(msg.op);
  if (op.get()->op_type == 0) {
    ClientReplyMessage reply;
    reply.success = true;
    reply.guidance = m_guide;
    reply.from = m_me;
    reply.key_hash = msg.key_hash;
    reply.reply_data = m_storage[msg.key_hash];
    LOG_F(3, "reply client read, data size: %zu", reply.reply_data.size());

    trans->send(reply);
    return;
  }

  auto new_idx = Morphling::find_target_smr(msg.key_hash).handle_operation(msg);
  m_client_pendings[new_idx] = std::move(trans);
  prepare_msgs();
}

void Morphling::handle_append_entries(AppendEntriesMessage &msg, int from) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(m_network[from]);
    return;
  }
  LOG_F(INFO, "recv AppendEntriesMessage, index: %zu", msg.entry.index);
  m_smrs[msg.group_id].handle_append_entries(msg, from);
  prepare_msgs();
}


void Morphling::handle_append_entries_reply(AppendEntriesReplyMessage &msg, int from) {
  if (!is_valid_guidance(msg.term)) {

    reply_guidance(m_network[from]);
    return;
  }
  LOG_F(INFO, "recv AppenEntriesReplyMessage, index: %zu", msg.index);
  m_smrs[msg.group_id].handle_append_entries_reply(msg, from);
  prepare_msgs();
}

void Morphling::reply_guidance(TransPtr &trans) {
  GuidanceMessage msg;
  msg.from = m_me;
  msg.guide = m_guide;
  msg.votes = 3;
  trans->send(msg);
}

#if 0
bool Morphling::maybe_apply() {
  for (auto &e : m_apply_entries) {
    auto op = parse_operation(e.data);
    LOG_F(INFO, "apply entry: %s, op: %d, key: 0x%8x", e.debug().c_str(), op->op_type, op->key_hash);
    ClientReplyMessage reply;
    reply.success = true;
    reply.guidance = m_guide;
    reply.from = m_me;
    reply.key_hash = op->key_hash;
    if (op->op_type == 1) {
      m_storage[op->key_hash] = op->data;
    } else {
      reply.reply_data = m_storage[op->key_hash];
    }

    m_client_pendings[e.index].send(reply);
  }
  m_apply_entries.clear();
}
#endif

Guidance& Morphling::get_guidance() {
  return m_guide;
}

std::vector<Entry>& Morphling::debug_apply_entries() {
  return m_apply_entries;
}