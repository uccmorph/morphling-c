#ifndef __CORE_MORPHLING_H__
#define __CORE_MORPHLING_H__

#include "smr.h"


const int DEFAULT_KEY_SPACE = 256;
const uint64_t DEFAULT_KEY_MASK = 0xff00;
const int DEFAULT_MASK_OFFSET = 8;

class Morphling {
  SMRLog m_log;
  guidance_t m_guide;
  int m_me;
  std::vector<int> m_peers;

 private:
  void init_local_guidance(int key_space = DEFAULT_KEY_SPACE);
  int calc_key_pos(uint64_t key_hash);



 public:
  Morphling(int id, std::vector<int> &peers);
  ~Morphling();

  void handle_operation(ClientProposalMessage &msg);
  void handle_append_entries(AppendEntriesMessage &msg);
  void handle_append_entries_reply(AppenEntriesReplyMessage &msg, int from);
  void send_append_entries(int to);
  void reply_client();
  bool is_valid_guidance(uint64_t epoch);
  void reply_guidance();
  void send_append_entries_reply(AppenEntriesReplyMessage &reply);
};

#endif // __CORE_MORPHLING_H__