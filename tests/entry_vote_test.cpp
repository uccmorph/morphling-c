#include <gtest/gtest.h>

#include "smr.h"

TEST(ExampleTest, EntryVoteTest) {
  EntryVoteRecord entry_votes;
  bool res = false;
  entry_votes.init(3);
  res = entry_votes.vote(2, 2);
  EXPECT_EQ(res, false) << "first vote can not reach quorum";
  res = entry_votes.vote(1, 2);
  EXPECT_EQ(res, true) << "second vote should reach quorum";
  res = entry_votes.vote(0, 2);
  EXPECT_EQ(res, false) << "further vote can not reach quorum";
  res = entry_votes.vote(1, 2);
  EXPECT_EQ(res, false) << "further vote can not reach quorum";
}