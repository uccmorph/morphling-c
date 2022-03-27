#include <atomic>
#include <memory>

class LockFreeQueue {
private:
  struct node {
    std::unique_ptr<uint8_t[]> data;
    node *next = nullptr;
  };
  std::atomic<node *> head;
  std::atomic<node *> tail;

  node *pop_head() {
    node *old_head = head.load();
    if (old_head == tail.load()) {
      return nullptr;
    }
    head.store(old_head->next);
    return old_head;
  }
public:
  LockFreeQueue(): head(new node), tail(head.load()) {}

  std::unique_ptr<uint8_t[]> pop() {
    node *old_head = pop_head();
    if (!old_head) {
      return std::unique_ptr<uint8_t[]>();
    }
    std::unique_ptr<uint8_t[]> res = std::move(old_head->data);
    delete old_head;
    return res;
  }

  void push(uint8_t *data, size_t size) {
    node *p = new node;
    node *old_tail = tail.load();
    old_tail->next = p;

    std::unique_ptr<uint8_t[]> new_data = std::make_unique<uint8_t[]>(size);
    std::copy(data, data+size, new_data.get());

    old_tail->data = std::move(new_data);
    tail.store(p);
  }
};

int main() {
  const size_t array_size = 10;
  uint8_t *data_original = new uint8_t[array_size];
  for (size_t i = 0; i < array_size; i++) {
    data_original[i] = uint8_t(i);
  }

  const size_t set_size = 5;
  uint8_t *data_set[set_size];
  for (size_t s = 0; s < set_size; s++) {
    data_set[s] = new uint8_t[array_size];
    for (size_t i = 0; i < array_size; i++) {
      data_set[s][i] = uint8_t(i*(s+1));
    }
  }

  for (size_t s = 0; s < set_size; s++) {
    for (size_t i = 0; i < array_size; i++) {
      printf("0x%02x ", data_set[s][i]);
    }
    printf("\n");
  }

  LockFreeQueue q;

  for (size_t s = 0; s < set_size; s++) {
    q.push(data_set[s], array_size);
  }

  auto print_res = [](std::unique_ptr<uint8_t[]> &&data) {
    printf("data at: %p\n", data.get());
    for (size_t i = 0; i < array_size; i++) {
      printf("0x%02x ", data[i]);
    }
    printf("\n");
  };

  for (size_t i = 0; i < 8; i++) {
    print_res(q.pop());
  }

  // std::unique_ptr<uint8_t[]> res = std::make_unique<uint8_t[]>(array_size);
  // std::unique_ptr<uint8_t> res(data_original);
  // std::unique_ptr<uint8_t> res(new std::remove_extent_t<uint8_t>[array_size]);

  // std::copy(data_original, data_original+array_size, res.get());

  // for (size_t i = 0; i < array_size; i++) {
  //   printf("0x%02x ", res[i]);
  // }
  // printf("\n");

}