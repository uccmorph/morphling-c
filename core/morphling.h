#ifndef __CORE_MORPHLING_H__
#define __CORE_MORPHLING_H__

#include <memory>

#include "smr.h"
#include "transport.h"


const int DEFAULT_KEY_SPACE = 256;
const uint64_t DEFAULT_KEY_MASK = 0xff00ul;
const int DEFAULT_MASK_OFFSET = 8;

int calc_key_pos(uint64_t key_hash);

using TransPtr = std::unique_ptr<Transport>;

class Morphling {
  Guidance m_guide;
  int m_me;
  std::vector<int> m_peers;
  std::vector<SMR> m_smrs;
  std::vector<Entry> m_apply_entries;
  std::unordered_map<uint64_t, TransPtr> m_client_pendings;

  std::unordered_map<int, TransPtr> m_network;

  // simple memory storage
  std::unordered_map<uint64_t, std::vector<uint8_t>> m_storage;

  // used for recv
  MessageBuffer<ClientRawMessage> m_prealloc_client_recv;
  MessageBuffer<AppendEntriesRawMessage> m_prealloc_append_recv;
  MessageBuffer<AppendEntriesReplyMessage> m_prealloc_areply_recv;

  // used for send
  MessageBuffer<ClientReplyRawMessage> m_prealloc_client_reply;;
  std::unordered_map<int, MessageBuffer<AppendEntriesRawMessage>> m_prealloc_ae_bcast;
  AppendEntriesReplyMessage m_prealloc_ae_reply;

 private:
  void init_local_guidance(int key_space = DEFAULT_KEY_SPACE);
  bool is_valid_guidance(uint64_t epoch);
  SMR& find_target_smr(uint64_t key_hash);

  OperationRaw& parse_operation(std::vector<uint8_t> &data);

 public:
  Morphling(int id, std::vector<int> &peers);
  ~Morphling();

  void handle_message(MessageHeader &header, TransPtr &trans);

  void handle_operation(ClientRawMessage &msg, TransPtr &trans);
  void handle_append_entries(AppendEntriesRawMessage &msg);
  void handle_append_entries_reply(AppendEntriesReplyMessage &msg);
  void reply_guidance(TransPtr &trans);
  void reply_client(int rw_type, uint64_t key_hash, bool success, TransPtr &trans);

  Guidance& get_guidance();
  void set_peer_trans(int id, TransPtr &trans);
  TransPtr& get_peer_trans(int id);
};

#endif // __CORE_MORPHLING_H__