#include <gtest/gtest.h>

#include "smr.h"

TEST(ReplicationTest, NewEntryTest) {
  std::vector<int> peers{0, 1, 2};
  std::string op_data("hello world!");
  srand(time(0));
  for (int id = 0; id < 3; id++) {
    auto gid = rand() % 100;
    auto term = rand() % 100;
    std::vector<AppendEntriesRawMessage *> out_msgs;
    message_cb_t<AppendEntriesRawMessage> cb =
        [&out_msgs](AppendEntriesRawMessage &msg, int to) -> bool {
      AppendEntriesRawMessage *new_msg =
          (AppendEntriesRawMessage
               *)new uint8_t[sizeof(AppendEntriesRawMessage) +
                             msg.entry.data_size];
      *new_msg = msg;
      memcpy(new_msg->entry.get_op_buf(), msg.entry.get_op_buf(),
             msg.entry.data_size);
      printf("entry op size: %zu\n", msg.entry.data_size);
      out_msgs.push_back(new_msg);
      return true;
    };

    std::unordered_map<int, MessageBuffer<AppendEntriesRawMessage>>
        pre_alloc_ae_bcast;
    for (auto id : peers) {
      pre_alloc_ae_bcast.try_emplace(id);
    }
    ae_cb_t ae_cb = [&pre_alloc_ae_bcast](int id) -> AppendEntriesRawMessage & {
      printf("calling %s, id = %d, max msgbuf size: %zu\n", __PRETTY_FUNCTION__,
             id, pre_alloc_ae_bcast[id].size);
      return pre_alloc_ae_bcast[id].to_message();
    };
    SMR smr(id, peers);
    smr.set_cb(cb, nullptr, nullptr);
    smr.set_pre_alloc_ae_cb(ae_cb);
    smr.set_gid(gid);
    smr.set_term(term);

    ClientRawMessage *msg =
        (ClientRawMessage
             *)new uint8_t[sizeof(ClientRawMessage) + op_data.size()];
    msg->term = term;
    msg->key_hash = 0x4199;
    msg->data_size = op_data.size();
    std::copy(op_data.begin(), op_data.end(), msg->get_op_buf());
    printf("id = %d, prepare to handle op\n", id);
    smr.handle_operation(*msg);
    printf("id = %d, finish op\n", id);
    EXPECT_EQ(out_msgs.size(), 2);
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(out_msgs[i]->from, id);
      EXPECT_EQ(out_msgs[i]->term, term);
      EXPECT_EQ(out_msgs[i]->group_id, gid);
      EXPECT_EQ(out_msgs[i]->prev_index, 0);
      EXPECT_EQ(out_msgs[i]->prev_term, 0);

      EXPECT_EQ(out_msgs[i]->entry.index, 1);
      EXPECT_EQ(out_msgs[i]->entry.term, term);
      EXPECT_EQ(out_msgs[i]->entry.data_size, op_data.size());
    }

    printf("id = %d, prepare to handle op\n", id);
    smr.handle_operation(*msg);
    printf("id = %d, finish op\n", id);
    EXPECT_EQ(out_msgs.size(), 4);
    for (int i = 2; i < 4; i++) {
      EXPECT_EQ(out_msgs[i]->from, id);
      EXPECT_EQ(out_msgs[i]->term, term);
      EXPECT_EQ(out_msgs[i]->group_id, gid);
      EXPECT_EQ(out_msgs[i]->prev_index, 1);
      EXPECT_EQ(out_msgs[i]->prev_term, term);

      EXPECT_EQ(out_msgs[i]->entry.index, 2);
      EXPECT_EQ(out_msgs[i]->entry.term, term);
      EXPECT_EQ(out_msgs[i]->entry.data_size, op_data.size());
    }

    for (auto &ae : out_msgs) {
      printf("append entry: %s\n", ae->debug().c_str());
    }
  }
}

TEST(ReplicationTest, HandelAppendEntryTest) {
  std::vector<AppendEntriesRawMessage *> test_in;
  std::string op_data("hello world!");
  for (int i = 0; i < 3; i++) {
    AppendEntriesRawMessage *ae_raw =
        (AppendEntriesRawMessage
             *)new uint8_t[sizeof(AppendEntriesRawMessage) + op_data.size()];
    ae_raw->entry.term = 1;
    ae_raw->entry.index = i + 1;
    ae_raw->entry.data_size = op_data.size();
    std::copy(op_data.begin(), op_data.end(), ae_raw->entry.get_op_buf());

    ae_raw->from = 0;
    ae_raw->group_id = 42;
    ae_raw->commit = 0;
    ae_raw->term = 1;
    ae_raw->prev_term = uint64_t((i == 0) ? 0 : 1);
    ae_raw->prev_index = uint64_t(i);

    test_in.push_back(ae_raw);
  }

  std::vector<int> peers{0, 1, 2};
  std::vector<AppendEntriesReplyMessage> out_msgs;
  message_cb_t<AppendEntriesReplyMessage> cb =
      [&out_msgs](AppendEntriesReplyMessage &msg, int to) -> bool {
    out_msgs.push_back(msg);
    return true;
  };
  SMR smr(1, peers);
  smr.set_term(1);
  smr.set_gid(42);
  smr.set_cb(nullptr, cb, nullptr);

  AppendEntriesReplyMessage prealloc_reply;
  for (auto &msg : test_in) {
    printf("append entry: %s\n", msg->debug().c_str());
    smr.handle_append_entries(*msg, prealloc_reply);
  }
  auto &log = smr.debug_get_log();
  printf("%s\n", log.debug().c_str());
  EXPECT_EQ(log.last_index(), 3);
  EXPECT_EQ(log.curr_commit(), 0);

  uint64_t expect_idx = 1;
  for (auto &item : out_msgs) {
    EXPECT_EQ(item.from, 1);
    EXPECT_TRUE(item.success);
    EXPECT_EQ(item.group_id, 42);
    EXPECT_EQ(item.index, expect_idx);
    EXPECT_EQ(item.term, 1);
    expect_idx += 1;
  }
}

#if 1
TEST(ReplicationTest, HandelAppendEntryReplyTest) {
  std::vector<AppendEntriesReplyMessage> test_in{
      AppendEntriesReplyMessage{
          .from = 1,
          .success = true,
          .index = 1,
      },
      AppendEntriesReplyMessage{
          .from = 1,
          .success = true,
          .index = 2,
      },
      AppendEntriesReplyMessage{
          .from = 2,
          .success = true,
          .index = 1,
      },
      AppendEntriesReplyMessage{
          .from = 2,
          .success = true,
          .index = 2,
      },
  };

  std::vector<int> peers{0, 1, 2};
  std::vector<Entry> out_entries;
  apply_cb_t cb = [&out_entries](SMRLog &log, size_t start,
                                 size_t end) -> bool {
    for (size_t idx = start; idx <= end; idx++) {
      out_entries.push_back(log.entry_at(idx));
    }
    return true;
  };

  SMR smr(2, peers);
  smr.set_term(2);
  smr.set_gid(42);
  smr.set_cb(nullptr, nullptr, cb);

  ClientRawMessage cmsg;
  smr.handle_operation(cmsg);
  smr.handle_operation(cmsg);

  smr.handle_append_entries_reply(test_in[0]);
  smr.handle_append_entries_reply(test_in[1]);
  EXPECT_EQ(out_entries.size(), 2);

  auto &log = smr.debug_get_log();
  EXPECT_EQ(log.curr_commit(), 2);

  smr.handle_append_entries_reply(test_in[2]);
  smr.handle_append_entries_reply(test_in[3]);

  printf("log: %s\n", log.debug().c_str());

  EXPECT_EQ(out_entries.size(), 2);

  uint64_t index = 1;
  for (auto &item : out_entries) {
    printf("to apply entry term: %zu, index: %zu\n", item.term, item.index);
    EXPECT_EQ(item.term, 2);
    EXPECT_EQ(item.index, index);
    index += 1;
  }
}
#endif