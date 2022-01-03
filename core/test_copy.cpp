#include <stdio.h>
#include <string.h>

#include "message.h"
#include "smr.h"



int main() {
  size_t data_size = 100;
  uint8_t *data = new uint8_t[data_size];
  memset(data, 'a', data_size);
  for (size_t i = 0; i < data_size; i++) {
    printf("0x%x ", data[i]);
  }
  printf("\n");

  uint8_t buf[2048];
  Operation op{
      .op_type = 1,
      .key_hash = 0x5499,
  };
  op.data.resize(data_size);
  std::copy(data, data + data_size, op.data.begin());

  for (size_t i = 0; i < op.data.size(); i++) {
    printf("0x%x ", op.data[i]);
  }
  printf("\n");
}