#include <gtest/gtest.h>

#include "message.h"
#include "morphling.h"
#include "transport.h"

class MockTransport: public Transport {
public:
  ~MockTransport() {}
  void send(uint8_t *buf, uint64_t size) {
    printf("send data %zu bytes\n", size);
  }
  void send(AppendEntriesMessage &msg) {
    printf("send AppendEntriesMessage\n");
  }
  void send(AppenEntriesReplyMessage &msg) {
    printf("send AppenEntriesReplyMessage\n");
  }
  void send(ClientMessage &msg) {
    printf("send ClientMessage\n");
  }
  void send(GuidanceMessage &msg) {
    printf("send GuidanceMessage\n");
  }
};

TEST(BasicMorphlingTest, ClientOperationTest) {
  std::vector<int> peers{0, 1, 2};
  Morphling mpreplica(1, peers);

  std::string data("hello world!");
  Operation op{
    .op_type = 1,
    .key_hash = 0x4199,
    .data = std::vector<uint8_t>(data.begin(), data.end()),
  };
  std::stringstream ss;
  msgpack::pack(ss, op);
  const std::string &op_str = ss.str();

  ClientMessage msg{
      .epoch = 1,
      .key_hash = 0x4199,
      .op = std::vector<uint8_t>(op_str.begin(), op_str.end()),
  };

  std::unique_ptr<Transport> trans = std::make_unique<MockTransport>();
  mpreplica.handle_operation(msg, std::move(trans));

  auto &next_msgs = mpreplica.debug_next_msgs();
  EXPECT_EQ(next_msgs.size(), 2);

  for (auto &msg : next_msgs) {
    printf("msg type %d, to %d\n", msg.type, msg.to);
  }

}