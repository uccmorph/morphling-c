#include <gtest/gtest.h>

#include "smr.h"

std::vector<Entry> generate_test_entries(size_t size) {
  std::vector<Entry> log;
  std::string str("hello world");
  std::vector<uint8_t> data(str.begin(), str.end());

  for (size_t i = 0; i < size; i++) {
    log.emplace_back(data);
  }

  return log;
}

TEST(SMRLogTest, AppendNGetTest) {
  size_t log_size = 3;
  auto test_entries = generate_test_entries(log_size);

  SMRLog smr_log;
  EXPECT_EQ(smr_log.entry_at(0).index, 0);
  EXPECT_EQ(smr_log.last_index(), 0);
  for (size_t i = 0; i < log_size; i++) {
    auto idx = smr_log.append(test_entries[i]);
    EXPECT_EQ(smr_log.entry_at(idx).index, i+1);

    EXPECT_EQ(smr_log.last_index(), i+1);
  }
  printf("%s\n", smr_log.debug().c_str());
}

TEST(SMRLogTest, TruncateTest) {
  size_t log_size = 5;
  auto test_entries = generate_test_entries(log_size);

  SMRLog smr_log;
  EXPECT_EQ(smr_log.last_index(), 0);
  for (size_t i = 0; i < log_size; i++) {
    smr_log.append(test_entries[i]);
  }

  smr_log.truncate(3);
  EXPECT_EQ(smr_log.last_index(), 2);
  printf("%s\n", smr_log.debug().c_str());

  // expect 3, 4, 5, 6, 7
  for (size_t i = 0; i < log_size; i++) {
    auto idx = smr_log.append(test_entries[i]);

    EXPECT_EQ(smr_log.entry_at(idx).index, i+3);

    EXPECT_EQ(smr_log.last_index(), i+3);
  }
  printf("%s\n", smr_log.debug().c_str());
}

TEST(SMRLogTest, CommitTest) {
  size_t log_size = 5;
  auto test_entries = generate_test_entries(log_size);

  SMRLog smr_log;
  EXPECT_EQ(smr_log.curr_commit(), 0);

  for (size_t i = 0; i < log_size; i++) {
    smr_log.append(test_entries[i]);
  }

  auto res = smr_log.commit_to(2);
  EXPECT_TRUE(res);
  res = smr_log.commit_to(2);
  EXPECT_FALSE(res);

  res = smr_log.commit_to(5);
  EXPECT_TRUE(res);

  printf("%s\n", smr_log.debug().c_str());
}

TEST(SMRLogTest, HeavyAppendTest) {
  size_t log_size = 100000;
  auto test_entries = generate_test_entries(log_size);
  size_t value_size = test_entries[0].data.size();
  SMRLog smr_log;
  EXPECT_EQ(smr_log.entry_at(0).index, 0);
  EXPECT_EQ(smr_log.last_index(), 0);
  for (size_t i = 0; i < log_size; i++) {
    auto idx = smr_log.append(test_entries[i]);
    EXPECT_EQ(smr_log.entry_at(idx).index, i+1);
    EXPECT_EQ(smr_log.last_index(), i+1);
    EXPECT_EQ(smr_log.entry_at(idx).data.size(), value_size);
  }
  // printf("%s\n", smr_log.debug().c_str());
}