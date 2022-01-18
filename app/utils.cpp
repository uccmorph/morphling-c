

#include "utils.h"

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


