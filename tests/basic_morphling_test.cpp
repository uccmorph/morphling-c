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

void print_buffer(uint8_t *buf, size_t size) {
  uint32_t *tmp = (uint32_t *)buf;
  for (size_t i = 0; i < size/sizeof(uint32_t); i++) {
    printf("0x%08x ", tmp[i]);
  }
  printf("\n");
}

TEST(BasicMorphlingTest, ClientOperationTest) {
  std::vector<int> peers{0, 1, 2};
  int self = 1;
  Morphling mpreplica(self, peers);

  std::string data("hello world!");
  for (auto id : peers) {
    TransPtr trans = std::make_unique<MockTransport>();
    mpreplica.set_peer_trans(id, trans);
  }

  size_t data_size = data.size();

  size_t op_size = sizeof(OperationRaw) + data_size;
  uint8_t *cmsg_buf = new uint8_t[sizeof(ClientRawMessage) + op_size];
  ClientRawMessage &msg_raw = *reinterpret_cast<ClientRawMessage *>(cmsg_buf);
  msg_raw.header.type = MessageType::MsgTypeClient;
  msg_raw.header.size = sizeof(ClientRawMessage) + op_size;
  msg_raw.term = 1;
  msg_raw.key_hash = 0x4199;
  msg_raw.data_size = op_size;

  OperationRaw &op = msg_raw.get_op();
  op.op_type = 1;
  op.key_hash = msg_raw.key_hash;
  op.value_size = data_size;
  std::copy(data.begin(), data.end(), op.get_value_buf());

  print_buffer(cmsg_buf, sizeof(ClientRawMessage) + op_size);

  std::unique_ptr<Transport> trans = std::make_unique<MockTransport>();
  mpreplica.handle_operation(msg_raw, trans);

  for (auto id : peers) {
    if (id == self) {
      continue;
    }

    MockTransport *mock_trans = dynamic_cast<MockTransport *>(mpreplica.get_peer_trans(0).get());
    if (mock_trans == nullptr) {
      printf("dynamic cast failed\n");
    }
    for (auto &raw_msg : mock_trans->sent_msgs) {
      AppendEntriesRawMessage *msg = (AppendEntriesRawMessage *)raw_msg.data();
      EXPECT_EQ(msg->from, 1);
      EXPECT_EQ(msg->term, 1);
      EXPECT_EQ(msg->entry.index, 1);
      EXPECT_EQ(msg->entry.get_op().value_size, data_size);
      print_buffer(msg->entry.get_op().get_value_buf(), msg->entry.get_op().value_size);
    }
  }
}