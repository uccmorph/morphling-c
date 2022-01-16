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
  // std::vector<GenericMessage> m_next_msgs;
  std::vector<Entry> m_apply_entries;
  std::unordered_map<uint64_t, TransPtr> m_client_pendings;

  std::unordered_map<int, TransPtr> m_network;

  // simple memory storage
  std::unordered_map<uint64_t, std::vector<uint8_t>> m_storage;

 private:
  void init_local_guidance(int key_space = DEFAULT_KEY_SPACE);
  bool is_valid_guidance(uint64_t epoch);
  SMR& find_target_smr(uint64_t key_hash);
  bool prepare_msgs();

  std::unique_ptr<Operation> parse_operation(std::vector<uint8_t> data);


 public:
  Morphling(int id, std::vector<int> &peers);
  ~Morphling();

  void handle_operation(ClientMessage &msg, TransPtr &trans);
  void handle_append_entries(AppendEntriesMessage &msg, int from);
  void handle_append_entries_reply(AppendEntriesReplyMessage &msg, int from);


  // void bcast_msgs(std::unordered_map<int, TransPtr> &peers_trans);
  // bool maybe_apply();
  void reply_guidance(TransPtr &trans);

  Guidance& get_guidance();
  TransPtr& get_peer_trans(int id);
  // std::vector<GenericMessage>& debug_next_msgs();
  std::vector<Entry>& debug_apply_entries();
};

#endif // __CORE_MORPHLING_H__