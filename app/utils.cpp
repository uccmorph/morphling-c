#include <cstdint>
#include <string>
#include <vector>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

void parse_addr(const char *cmd_arg, std::vector<std::string> &peers_addr) {
  std::string arg(cmd_arg);
  size_t pos = 0;
  size_t old_pos = pos;
  while (true) {
    pos = arg.find(",", old_pos);
    if (pos == std::string::npos) {
      peers_addr.push_back(arg.substr(old_pos));
      break;
    } else {
      peers_addr.push_back(arg.substr(old_pos, pos - old_pos));
      old_pos = pos + 1;
    }
  }
}

void pack_operation(Operation &op, ClientMessage &msg, uint8_t *dest, size_t &size) {
  std::stringstream ss;
  msgpack::pack(ss, op);
  const std::string &op_str = ss.str();

  msg.op = std::vector<uint8_t>(op_str.begin(), op_str.end());
  std::stringstream ss2;
  msgpack::pack(ss2, msg);
  const std::string &msg_buf = ss2.str();
  uint32_t msg_size = msg_buf.size();
  LOG_F(3, "msg_size = 0x%08x", msg_size);
  for (int i = 0; i < 3; i++) {
    LOG_F(3, "after shift %d: %x", i * 2, (msg_size >> (i * 8)));
    dest[i] = (msg_size >> (i * 8)) & 0xFF;
    LOG_F(3, "buf %d is 0x%x", i, dest[i]);
  }
  dest[3] = MessageType::MsgTypeClient & 0xFF;

  assert(msg_buf.size() < 2000);
  memcpy(&dest[4], msg_buf.c_str(), msg_buf.size());
  size = 4 + msg_buf.size();
}

void pack_get_guiddance(uint8_t *dest, size_t &size) {
  memset(dest, 0, 4);
  dest[3] = MessageType::MsgTypeGetGuidance & 0xFF;
  size = 4;
}

void full_read(struct bufferevent *bev, uint8_t *buf, size_t size) {
  size_t remain_size = size;
  size_t turn_read_n = 0;
  size_t finish_read_n = 0;
  while (remain_size > 0) {
    turn_read_n = bufferevent_read(bev, buf + finish_read_n, remain_size);
    remain_size -= turn_read_n;
    finish_read_n += turn_read_n;
  }
}

void drain_read(struct bufferevent *bev, uint8_t *buf, size_t size) {
  size_t turn_read_n = bufferevent_read(bev, buf, size);
  while (turn_read_n != 0) {
    turn_read_n = bufferevent_read(bev, buf, size);
  }
}

MessageType recv_msg(struct bufferevent *bev, uint8_t *buf, size_t &size) {
  constexpr size_t rpc_header_size = 4;
  full_read(bev, buf, rpc_header_size);
  uint32_t payload_size = 0;
  for (int i = 0; i < (int)rpc_header_size - 1; i++) {
    payload_size = payload_size | ((buf[i] & 0x000000FF) << (i * 8));
    LOG_F(5, "buf %d: 0x%x", i, buf[i]);
  }
  MessageType msg_type = (MessageType)(buf[3] & 0x000000FF);

  LOG_F(5, "payload size: %u", payload_size);
  LOG_F(5, "msg type: %d", msg_type);

  if (msg_type != MsgTypeAppend && msg_type != MsgTypeAppendReply &&
      msg_type != MsgTypeClient && msg_type != MsgTypeGuidance &&
      msg_type != MsgTypeGetGuidance) {
    return MsgTypeUnknown;
  }

  full_read(bev, buf, payload_size);
  size = payload_size;

  return msg_type;
}