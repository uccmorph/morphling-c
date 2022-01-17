#include <gtest/gtest.h>

#include "smr.h"

TEST(ReplicationTest, NewEntryTest) {
  std::vector<int> peers{0, 1, 2};
  srand(time(0));
  for (int id = 0; id < 3; id++) {
    auto gid = rand() % 100;
    auto term = rand() % 100;
    std::vector<AppendEntriesMessage> out_msgs;
    message_cb_t<AppendEntriesMessage> cb =
        [&out_msgs](AppendEntriesMessage &msg, int to) -> bool {
      out_msgs.push_back(msg);
      return true;
    };
    SMR smr(id, peers);
    smr.set_cb(cb, nullptr, nullptr);
    smr.set_gid(gid);
    smr.set_term(term);

    ClientMessage msg;
    msg.term = term;
    smr.handle_operation(msg);
    EXPECT_EQ(out_msgs.size(), 2);
    for (int i = 0; i < 2; i++) {
      EXPECT_EQ(out_msgs[i].from, id);
      EXPECT_EQ(out_msgs[i].term, term);
      EXPECT_EQ(out_msgs[i].group_id, gid);
      EXPECT_EQ(out_msgs[i].prev_index, 0);
      EXPECT_EQ(out_msgs[i].prev_term, 0);
    }

    smr.handle_operation(msg);
    EXPECT_EQ(out_msgs.size(), 4);
    for (int i = 2; i < 4; i++) {
      EXPECT_EQ(out_msgs[i].from, id);
      EXPECT_EQ(out_msgs[i].term, term);
      EXPECT_EQ(out_msgs[i].group_id, gid);
      EXPECT_EQ(out_msgs[i].prev_index, 1);
      EXPECT_EQ(out_msgs[i].prev_term, term);
    }

    for (auto &ae : out_msgs) {
      printf("append entry: %s\n", ae.debug().c_str());
    }
  }
}

TEST(ReplicationTest, HandelAppendEntryTest) {
  std::vector<AppendEntriesMessage> test_in;
  for (int i = 0; i < 3; i++) {
    Entry entry;
    entry.term = 1;
    entry.index = i + 1;
    AppendEntriesMessage append_msg{
        .prev_term = uint64_t((i == 0) ? 0 : 1),
        .prev_index = uint64_t(i),
        .commit = 0,
        .entry = entry,
    };
    test_in.push_back(append_msg);
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

  for (auto &msg : test_in) {
    smr.handle_append_entries(msg, 0);
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

  ClientMessage cmsg;
  smr.handle_operation(cmsg);
  smr.handle_operation(cmsg);

  smr.handle_append_entries_reply(test_in[0], 1);
  smr.handle_append_entries_reply(test_in[1], 1);
  EXPECT_EQ(out_entries.size(), 2);

  auto &log = smr.debug_get_log();
  EXPECT_EQ(log.curr_commit(), 2);

  smr.handle_append_entries_reply(test_in[2], 2);
  smr.handle_append_entries_reply(test_in[3], 2);

  printf("%s\n", log.debug().c_str());

  EXPECT_EQ(out_entries.size(), 2);

  for (auto &item : out_entries) {
    printf("to apply entry term: %zu, index: %zu\n", item.term, item.index);
  }
}
#endif