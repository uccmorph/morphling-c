#include <gtest/gtest.h>

#include "smr.h"

void debug_print_peer_msg(std::vector<GenericMessage> &out_msgs) {
  for (auto &item : out_msgs) {
    auto &entry = item.append_msg.entries;
    EXPECT_EQ(entry.size(), 1);
    printf(
        "out msg type: %d, to %d, prev epoch: %zu - index: %zu. "
        "entry epoch: %zu - index: %zu\n",
        item.type, item.to, item.append_msg.prev_term,
        item.append_msg.prev_index, entry.front().epoch, entry.front().index);
  }
}



TEST(ReplicationTest, NewEntryTest) {
  std::vector<int> peers{0, 1, 2};
  std::vector<GenericMessage> out_msgs;
  SMRMessageCallback cb{
      .notify_send_append_entry = [&out_msgs](GenericMessage &&msg) -> bool {
        out_msgs.push_back(msg);
        return true;
      },
  };
  SMR smr(0, peers, cb);

  ClientProposalMessage msg{
      .epoch = 1,
  };
  smr.handle_operation(msg);
  EXPECT_EQ(out_msgs.size(), 2);

  smr.handle_operation(msg);
  EXPECT_EQ(out_msgs.size(), 4);

  debug_print_peer_msg(out_msgs);
}

TEST(ReplicationTest, HandelAppendEntryTest) {

  std::vector<AppendEntriesMessage> test_in;
  for (int i = 0; i < 3; i++) {
    Entry entry(1, i+1);
    std::vector<Entry> entries{entry};
    AppendEntriesMessage append_msg{
      .prev_term = uint64_t((i == 0) ? 0 : 1),
      .prev_index = uint64_t(i),
      .commit = 0,
      .entries = entries,
    };
    test_in.push_back(append_msg);
  }

  std::vector<int> peers{0, 1, 2};
  std::vector<GenericMessage> out_msgs;
  SMRMessageCallback cb{
      .notify_send_append_entry_reply = [&out_msgs](GenericMessage &&msg) -> bool {
        out_msgs.push_back(msg);
        return true;
      },
  };
  SMR smr(1, peers, cb);

  for (auto itr = test_in.begin(); itr != test_in.end(); itr++) {
    auto &msg = (*itr);
    printf("handle msg %ld, epoch: %zu, index: %zu\n", itr - test_in.begin(), 
           msg.entries[0].epoch, msg.entries[0].index);
    smr.handle_append_entries(msg, 0);
  }
  auto &log = smr.debug_get_log();
  log.show_all();
  EXPECT_EQ(log.last_index(), 3);
  EXPECT_EQ(log.curr_commit(), 0);

  uint64_t expect_idx = 1;
  for (auto &item : out_msgs) {
    EXPECT_EQ(item.type, 2);
    EXPECT_EQ(item.to, 0);
    EXPECT_TRUE(item.append_reply_msg.success);
    EXPECT_EQ(item.append_reply_msg.index, expect_idx);
    expect_idx += 1;

    printf(
        "out msg type: %d, to %d, success: %d, index: %zu\n",
        item.type, item.to, item.append_reply_msg.success, item.append_reply_msg.index);
  }
}

TEST(ReplicationTest, HandelAppendEntryReplyTest) {
  std::vector<AppenEntriesReplyMessage> test_in{
    AppenEntriesReplyMessage{
      .success = true,
      .index = 1,
    },
    AppenEntriesReplyMessage{
      .success = true,
      .index = 2,
    },
    AppenEntriesReplyMessage{
      .success = true,
      .index = 1,
    },
    AppenEntriesReplyMessage{
      .success = true,
      .index = 2,
    },
  };

  std::vector<int> peers{0, 1, 2};
  std::vector<Entry> out_entries;
  std::vector<GenericMessage> out_msgs;
  SMRMessageCallback cb{
      .notify_send_append_entry = [&out_msgs](GenericMessage &&msg) -> bool {
        out_msgs.push_back(msg);
        return true;
      },
      .notify_apply = [&out_entries](std::vector<Entry> entries) -> bool {
        for (auto &item : entries) {
          out_entries.push_back(item);
        }
        return true;
      }};

  SMR smr(0, peers, cb);

  ClientProposalMessage msg{
      .epoch = 1,
  };
  smr.handle_operation(msg);
  smr.handle_operation(msg);

  smr.handle_append_entries_reply(test_in[0], 1);
  smr.handle_append_entries_reply(test_in[1], 1);
  EXPECT_EQ(out_entries.size(), 2);

  smr.handle_append_entries_reply(test_in[2], 2);
  smr.handle_append_entries_reply(test_in[3], 2);

  auto &log = smr.debug_get_log();
  log.show_all();

  EXPECT_EQ(out_entries.size(), 2);

  for (auto &item : out_entries) {
    printf("to apply entry epoch: %zu, index: %zu\n", item.epoch, item.index);
  }
}