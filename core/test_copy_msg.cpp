// This file is a "Hello, world!" in C++ language by GCC for wandbox.
#include <iostream>
#include <cstdlib>
#include <vector>
#include <unordered_map>

struct Frame {
    int a1 = 10;
    int a2 = 20;
    std::vector<int> arr;
};

int main()
{
    std::vector<Frame> v1;

    for (int i = 0; i < 10; i++) {
        v1.push_back(Frame{i * 10, (i + 1) * 10});
        v1[i].arr = std::vector<int>{1, 2, 3};
    }

    std::vector<Frame> v2;
    v2.reserve(7);
    for (int i = 0; i < 10; i++) {
        v2.push_back(Frame{i * 10, (i + 1) * 10});
    }
    for (auto &item : v2) {
        printf("a1 = %d\n", item.a1);
    }

    v2.assign(v1.begin() + 5, v1.end());

    size_t copy_num = v1.end() - v1.begin() - 5;

    printf("total copy: %zu, v2 size: %zu\n", copy_num, v2.size());

    for (auto &item : v2) {
        printf("a1 = %d\n", item.a1);
    }
}
