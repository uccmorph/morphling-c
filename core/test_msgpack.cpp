#include <sstream>
#include <iostream>
#include <memory>

#include <msgpack.hpp>

#include "message.h"
#include "smr.h"
#include "guidance.h"

static void print(std::string const& buf) {
    for (std::string::const_iterator it = buf.begin(), end = buf.end();
         it != end;
         ++it) {
        std::cout
            << std::setw(2)
            << std::hex
            << std::setfill('0')
            << (static_cast<int>(*it) & 0xff)
            << ' ';
    }
    std::cout << std::dec << std::endl;
}

std::string send() {
  std::vector<Entry> ens{
    Entry(2, 5),
  };

  AppendEntriesMessage msg{
    .epoch = 2,
    .prev_term = 2,
    .prev_index = 4,
    .commit = 1,
    .group_id = 10,
    .entries = ens,
  };

  std::stringstream stream;
  msgpack::pack(stream, msg);

  std::string const str = stream.str();
  print(str);

  return str;
}

void receive(std::string str) {
  auto oh = msgpack::unpack(str.data(), str.size());
  auto o = oh.get();

  std::unique_ptr<AppendEntriesMessage> p_msg = std::make_unique<AppendEntriesMessage>();
  AppendEntriesMessage& msg = *p_msg.get();
  o.convert(msg);

  printf("msg: poch: %zu, prev_epoch: %zu, prev_index: %zu, commit: %zu, group_id: %zu\n", msg.epoch, msg.prev_term, msg.prev_index, msg.commit, msg.group_id);
  printf("entries size: %zu\n", msg.entries.size());
  for (auto &item : msg.entries) {
    printf("item, epoch: %zu, index: %zu\n", item.epoch, item.index);
  }
}

std::string send_guidance() {
  GuidanceMessage msg{
    .guide = guidance_t{
      .epoch = 2,
      .status = 0x00000303,
      .cluster = {
        node_status_t{
          .status = 0x00015500
        },
        node_status_t{
          .status = 0x0001AA56
        },
        node_status_t{
          .status = 0x01FFAB
        },
      }
    }
  };

  printf("size of msg: %zu\n", sizeof(msg));

  std::stringstream stream;
  msgpack::pack(stream, msg);

  return stream.str();
}

void receive_guidance(std::string str) {
  auto oh = msgpack::unpack(str.data(), str.size());
  auto o = oh.get();

  GuidanceMessage msg;
  o.convert(msg);

  guidance_t *g = &msg.guide;
  debug_print_guidance(g);
}

int main() {

  auto buf_str = send();

  receive(buf_str);

  printf("------------ guidance msg test -------------\n");
  auto gui_data = send_guidance();

  receive_guidance(gui_data);


}