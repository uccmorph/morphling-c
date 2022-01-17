
#include <loguru.hpp>

#include "morphling.h"

int calc_key_pos(uint64_t key_hash) {
  return (key_hash & DEFAULT_KEY_MASK) >> DEFAULT_MASK_OFFSET;
}

class NullTransport: public Transport {
public:
  ~NullTransport() {}
  bool is_ready() {
    return false;
  }
  void send(uint8_t *buf, uint64_t size) {}
};

Morphling::Morphling(int id, std::vector<int> &peers)
    : m_me(id), m_peers(peers) {
  Morphling::init_local_guidance();
  m_storage.reserve(1000);

  for (int id : peers) {
      m_network.emplace(id, std::make_unique<NullTransport>());
  }

  message_cb_t<AppendEntriesMessage> ae_cb = [this](AppendEntriesMessage &msg, int to) -> bool {
    auto &trans = Morphling::get_peer_trans(to);
    if (!trans->is_ready()) {
      return false;
    }

    size_t body_size = sizeof(AppendEntriesRawMessage);
    size_t entry_data_size = msg.entry.data.size();
    uint8_t *msg_buf = new uint8_t[body_size + entry_data_size];
    AppendEntriesRawMessage &raw_msg = *reinterpret_cast<AppendEntriesRawMessage *>(msg_buf);

    raw_msg.header.type = MessageType::MsgTypeAppend;
    raw_msg.header.size = body_size + entry_data_size;
    raw_msg.from = msg.from;
    raw_msg.term = msg.term;
    raw_msg.prev_term = msg.prev_term;
    raw_msg.prev_index = msg.prev_index;
    raw_msg.commit = msg.commit;
    raw_msg.group_id = msg.group_id;

    raw_msg.entry.term = msg.entry.term;
    raw_msg.entry.index = msg.entry.index;
    raw_msg.entry.data_size = entry_data_size;
    raw_msg.copy_entry_data_in(msg.entry.data.data(), entry_data_size);

    trans->send(msg_buf, raw_msg.header.size);

    delete []msg_buf;
    return true;
  };
  message_cb_t<AppendEntriesReplyMessage> aer_cb = [this](AppendEntriesReplyMessage &msg, int to) -> bool {
    auto &trans = Morphling::get_peer_trans(to);
    if (!trans->is_ready()) {
      return false;
    }
    msg.header.type = MessageType::MsgTypeAppendReply;
    msg.header.size = sizeof(AppendEntriesReplyMessage);
    trans->send((uint8_t *)&msg, msg.header.size);
    return true;
  };
  apply_cb_t apply_cb = [this](SMRLog &log, size_t start, size_t end) -> bool {
    for (size_t idx = start; idx <= end; idx++) {
      Entry &e = log.entry_at(idx);
      OperationRaw &op_raw = Morphling::parse_operation(e.data);

      // apply write operation
      if (op_raw.op_type == 1) {
        uint8_t *value_buf = op_raw.get_value_buf();
        m_storage[op_raw.key_hash].reserve(op_raw.value_size);
        m_storage[op_raw.key_hash].assign(value_buf, value_buf + op_raw.value_size);
      }

      Morphling::reply_client(op_raw.op_type, op_raw.key_hash, Morphling::m_client_pendings[idx]);
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

OperationRaw& Morphling::parse_operation(std::vector<uint8_t> &data) {
  OperationRaw &op_raw = *reinterpret_cast<OperationRaw *>(data.data());

  return op_raw;
}

// void Morphling::handle_operation(ClientMessage &msg, TransPtr &trans) {
//   if (!is_valid_guidance(msg.term)) {
//     reply_guidance(trans);
//     return;
//   }

//   auto op = parse_operation(msg.op);
//   if (op.op_type == 0) {
//     auto &value = m_storage[msg.key_hash];
//     auto raw_reply = Morphling::new_client_reply(0, value.size());
//     ClientReplyRawMessage &reply = *(raw_reply);
//     reply.success = true;
//     reply.guidance = m_guide;
//     reply.from = m_me;
//     reply.key_hash = msg.key_hash;
//     reply.copy_data_in(value.data(), value.size());
//     LOG_F(3, "reply client read, data size: %zu", value.size());

//     trans->send((uint8_t *)raw_reply, raw_reply->header.size);

//     uint8_t *reply_buf = (uint8_t *)raw_reply;
//     delete []reply_buf;
//     return;
//   }

//   auto new_idx = Morphling::find_target_smr(msg.key_hash).handle_operation(msg);
//   m_client_pendings[new_idx] = std::move(trans);
// }

void Morphling::handle_operation(ClientRawMessage &msg, TransPtr &trans) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(trans);
    return;
  }

  OperationRaw *op = (OperationRaw *)msg.get_op_buf();
  if (op->op_type == 0) {
    reply_client(op->op_type, msg.key_hash, trans);
    return;
  }

  auto new_idx = Morphling::find_target_smr(msg.key_hash).handle_operation(msg);
  m_client_pendings[new_idx] = std::move(trans);
}

void Morphling::handle_append_entries(AppendEntriesMessage &msg, int from) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(m_network[from]);
    return;
  }
  LOG_F(INFO, "recv AppendEntriesMessage, index: %zu", msg.entry.index);
  m_smrs[msg.group_id].handle_append_entries(msg, from);
}


void Morphling::handle_append_entries_reply(AppendEntriesReplyMessage &msg, int from) {
  if (!is_valid_guidance(msg.term)) {

    reply_guidance(m_network[from]);
    return;
  }
  LOG_F(INFO, "recv AppenEntriesReplyMessage, index: %zu", msg.index);
  m_smrs[msg.group_id].handle_append_entries_reply(msg, from);
}

void Morphling::reply_guidance(TransPtr &trans) {
  GuidanceMessage msg;
  msg.header.type = MessageType::MsgTypeGuidance;
  msg.header.size = sizeof(GuidanceMessage);
  msg.from = m_me;
  msg.votes = 3;
  msg.guide = m_guide;
  trans->send((uint8_t *)&msg, msg.header.size);
}

void Morphling::reply_client(int rw_type, uint64_t key_hash, TransPtr &trans) {
  ClientReplyRawMessage &reply = m_prealloc_cr_msg.to_message();

  if (rw_type == 0) { // for read
    auto &value = m_storage[key_hash];
    size_t msg_size = sizeof(ClientReplyRawMessage) + value.size();
    assert(msg_size < m_prealloc_cr_msg.size);

    reply.header.type = MessageType::MsgTypeClientReply;
    reply.header.size = msg_size;

    reply.success = true;
    reply.guidance = m_guide;
    reply.from = m_me;
    reply.key_hash = key_hash;
    reply.copy_data_in(value.data(), value.size());
    LOG_F(3, "reply client read, data size: %zu", value.size());

    trans->send(m_prealloc_cr_msg.buf, reply.header.size);
  } else {

  }
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

void Morphling::set_peer_trans(int id, TransPtr &trans) {
  m_network[id] = std::move(trans);
}

TransPtr& Morphling::get_peer_trans(int id) {
  return m_network[id];
}

ClientReplyRawMessage* Morphling::new_client_reply(int op_type, size_t data_size) {
  size_t body_size = sizeof(ClientReplyRawMessage);
  if (op_type == 1) {
    data_size = 0;
  }
  uint8_t *reply_buf = new uint8_t[body_size + data_size];
  ClientReplyRawMessage &reply = *reinterpret_cast<ClientReplyRawMessage *>(reply_buf);
  reply.header.type = MessageType::MsgTypeClientReply;
  reply.header.size = body_size + data_size;

  return &reply;
}
