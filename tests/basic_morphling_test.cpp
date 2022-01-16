#include <gtest/gtest.h>

#include "message.h"
#include "morphling.h"
#include "transport.h"

class MockTransport: public Transport {
public:
  std::vector<std::vector<uint8_t>> sent_msgs;
  ~MockTransport() {}
  bool is_ready() {
    return true;
  }
  void send(uint8_t *buf, uint64_t size) {
    printf("send data %zu bytes\n", size);
    std::vector<uint8_t> raw_msg;
    raw_msg.assign(buf, buf + size);
    sent_msgs.push_back(raw_msg);
  }
};

TEST(BasicMorphlingTest, ClientOperationTest) {
  std::vector<int> peers{0, 1, 2};
  Morphling mpreplica(1, peers);

  std::string data("hello world!");
  for (auto id : peers) {
    TransPtr trans = std::make_unique<MockTransport>();
    mpreplica.set_peer_trans(id, trans);
  }

  size_t data_size = data.size();

  size_t op_size = sizeof(OperationRaw) + data_size;
  uint8_t *cmsg_buf = new uint8_t[sizeof(ClientRawMessge) + op_size];
  ClientRawMessge &cmsg = *reinterpret_cast<ClientRawMessge *>(cmsg_buf);
  cmsg.term = 1;
  cmsg.key_hash = 0x4199;
  cmsg.data_size = op_size;

  uint8_t *op_buf = cmsg_buf + sizeof(ClientRawMessge);
  OperationRaw &op = *reinterpret_cast<OperationRaw *>(op_buf);
  op.op_type = 1;
  op.key_hash = cmsg.key_hash;
  op.data_size = data.size();
  std::copy(data.begin(), data.end(), op_buf + sizeof(OperationRaw));

  for (size_t i = 0; i < sizeof(ClientRawMessge) + op_size; i++) {
    printf("0x%x ", cmsg_buf[i]);
  }
  printf("\n");

  ClientMessage msg;
  msg.term = cmsg.term;
  msg.key_hash = cmsg.key_hash;
  msg.op.assign(op_buf, op_buf + op_size);

  std::unique_ptr<Transport> trans = std::make_unique<MockTransport>();
  mpreplica.handle_operation(msg, trans);

  MockTransport *mock_trans = dynamic_cast<MockTransport *>(mpreplica.get_peer_trans(0).get());
  if (mock_trans == nullptr) {
    printf("dynamic cast failed\n");
  }
  for (auto &raw_msg : mock_trans->sent_msgs) {
    AppendEntriesRawMessage *msg = (AppendEntriesRawMessage *)raw_msg.data();
    EXPECT_EQ(msg->from, 1);
    EXPECT_EQ(msg->term, 1);
  }
}