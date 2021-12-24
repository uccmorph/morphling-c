#include <gtest/gtest.h>

#include "message.h"
#include "morphling.h"

TEST(BasicMorphlingTest, Init) {
  std::vector<int> peers{0, 1, 2};
  Morphling mpreplica(1, peers);

  ClientMessage msg{
      .epoch = 1,
      .key_hash = 0x4199,
  };

  // mpreplica.handle_operation(msg);


}