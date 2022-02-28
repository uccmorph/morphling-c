
#include "morphling.h"

#include <loguru.hpp>

int calc_key_pos(uint64_t key_hash) {
  return (key_hash & DEFAULT_KEY_MASK) >> DEFAULT_MASK_OFFSET;
}

class NullTransport : public Transport {
 public:
  ~NullTransport() {}
  bool is_ready() { return false; }
  void send(uint8_t *buf, uint64_t size) {}
  bool recv(uint8_t *recv_buf, uint64_t max_size) { return false; }
};

Morphling::Morphling(int id, std::vector<int> &peers)
    : m_me(id), m_peers(peers) {
  Morphling::init_local_guidance();
  m_storage.reserve(1000);

  for (int id : peers) {
    m_network.emplace(id, std::make_unique<NullTransport>());
    m_prealloc_ae_bcast.try_emplace(id);
  }

  message_cb_t<AppendEntriesRawMessage> ae_cb =
      [this](AppendEntriesRawMessage &msg, int to) -> bool {
    auto &trans = Morphling::get_peer_trans(to);
    if (!trans->is_ready()) {
      return false;
    }

    msg.header.type = MessageType::MsgTypeAppend;
    msg.header.size = sizeof(AppendEntriesRawMessage) + msg.entry.data_size;
    trans->send((uint8_t *)&msg, msg.header.size);
    return true;
  };
  message_cb_t<AppendEntriesReplyMessage> aer_cb =
      [this](AppendEntriesReplyMessage &msg, int to) -> bool {
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
        m_storage[op_raw.key_hash].assign(value_buf,
                                          value_buf + op_raw.value_size);
      }

      Morphling::reply_client(op_raw.op_type, op_raw.key_hash, true,
                              Morphling::m_client_pendings[idx]);
    }
    return true;
  };

  ae_cb_t alloc_ae_cb = [this](int id) -> AppendEntriesRawMessage & {
    return m_prealloc_ae_bcast[id].to_message();
  };
  // alloc enough space, so m_smrs won't automatically grow, and won't use move
  // constructor
  m_smrs.reserve(DEFAULT_KEY_SPACE);
  for (int i = 0; i < DEFAULT_KEY_SPACE; i++) {
    auto &smr = m_smrs.emplace_back(id, m_peers);
    smr.set_gid(i);
    smr.set_term(m_guide.term);
    smr.set_cb(ae_cb, aer_cb, apply_cb);
    smr.set_pre_alloc_ae_cb(alloc_ae_cb);
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

SMR &Morphling::find_target_smr(uint64_t key_hash) {
  auto group_id = calc_key_pos(key_hash);
  auto &smr = m_smrs[group_id];
  return smr;
}

OperationRaw &Morphling::parse_operation(std::vector<uint8_t> &data) {
  OperationRaw &op_raw = *reinterpret_cast<OperationRaw *>(data.data());

  return op_raw;
}

void Morphling::handle_message(MessageHeader &header, TransPtr &trans) {
  LOG_F(INFO, "handle new message, type: %d", header.type);
  if (header.type == MessageType::MsgTypeAppend) {
    assert(header.size < m_prealloc_append_recv.size);
    trans->recv(m_prealloc_append_recv.buf, header.size);
    handle_append_entries(m_prealloc_append_recv.to_message());

  } else if (header.type == MessageType::MsgTypeAppendReply) {
    assert(header.size < m_prealloc_areply_recv.size);
    trans->recv(m_prealloc_areply_recv.buf, header.size);
    handle_append_entries_reply(m_prealloc_areply_recv.to_message());

  } else if (header.type == MessageType::MsgTypeClient) {
    assert(header.size < m_prealloc_client_recv.size);
    trans->recv(m_prealloc_client_recv.buf, header.size);
    handle_operation(m_prealloc_client_recv.to_message(), trans);

  } else if (header.type == MessageType::MsgTypeGuidance) {
    uint8_t drain_buf[128];
    assert(header.size < 128);
    trans->recv(drain_buf, header.size);

  } else if (header.type == MessageType::MsgTypeGetGuidance) {
    uint8_t drain_buf[128];
    assert(header.size < 128);
    trans->recv(drain_buf, header.size);
    reply_guidance(trans);
  } else {
    LOG_F(WARNING, "don't know how to handle msg type: %d", header.type);
    uint8_t drain_buf[16];
    trans->recv(drain_buf, 16);
  }
}

void Morphling::handle_operation(ClientRawMessage &msg, TransPtr &trans) {
  OperationRaw *op = (OperationRaw *)msg.get_op_buf();

  if (!is_valid_guidance(msg.term)) {
    reply_client(op->op_type, msg.key_hash, false, trans);
    return;
  }

  if (op->op_type == 0) {
    reply_client(op->op_type, msg.key_hash, true, trans);
    return;
  }

  auto new_idx = Morphling::find_target_smr(msg.key_hash).handle_operation(msg);
  m_client_pendings[new_idx] = std::move(trans);
}

void Morphling::handle_append_entries(AppendEntriesRawMessage &msg) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(m_network[msg.from]);
    return;
  }
  LOG_F(INFO, "recv AppendEntriesMessage, index: %zu", msg.entry.index);
  m_smrs[msg.group_id].handle_append_entries(msg, m_prealloc_ae_reply);
}

void Morphling::handle_append_entries_reply(AppendEntriesReplyMessage &msg) {
  if (!is_valid_guidance(msg.term)) {
    reply_guidance(m_network[msg.from]);
    return;
  }
  LOG_F(INFO, "recv AppenEntriesReplyMessage, index: %zu", msg.index);
  m_smrs[msg.group_id].handle_append_entries_reply(msg);
}

void Morphling::reply_guidance(TransPtr &trans) {
  GuidanceMessage msg;
  msg.header.type = MessageType::MsgTypeGuidance;
  msg.header.size = sizeof(GuidanceMessage);
  msg.from = m_me;
  msg.votes = 3;
#ifdef CONFIG_GUIDANCE_LOCK
  while (m_guidance_lock_flag.test_and_set(std::memory_order_acquire)) {}
#endif
  msg.guide = m_guide;
#ifdef CONFIG_GUIDANCE_LOCK
  m_guidance_lock_flag.clear(std::memory_order_release)
#endif
  trans->send((uint8_t *)&msg, msg.header.size);
}

void Morphling::reply_client(int rw_type, uint64_t key_hash, bool success,
                             TransPtr &trans) {
  ClientReplyRawMessage &reply = m_prealloc_client_reply.to_message();

  reply.success = success;
  reply.guidance = m_guide;
  reply.from = m_me;
  reply.key_hash = key_hash;

  if (rw_type == 0) {  // for read
    auto &value = m_storage[key_hash];
    size_t msg_size = sizeof(ClientReplyRawMessage) + value.size();
    assert(msg_size < m_prealloc_client_reply.size);

    reply.header.type = MessageType::MsgTypeClientReply;
    reply.header.size = msg_size;
    std::copy(value.begin(), value.end(), reply.get_value_buf());

    LOG_F(3, "reply client read, data size: %zu", value.size());
  } else {
    reply.header.type = MessageType::MsgTypeClientReply;
    reply.header.size = sizeof(ClientReplyRawMessage);
  }
  trans->send(m_prealloc_client_reply.buf, reply.header.size);
}

Guidance &Morphling::get_guidance() { return m_guide; }

void Morphling::set_peer_trans(int id, TransPtr &trans) {
  m_network[id] = std::move(trans);
}

TransPtr &Morphling::get_peer_trans(int id) { return m_network[id]; }
