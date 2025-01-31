#include "lock-free-vector.cpp"

#include <thread>
#include <vector>
#include <cassert>
#include <iostream>
#include <random>
#include <chrono>

int generate_random(int min, int max) {
    static thread_local std::mt19937 generator(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

void test_single_threaded() {
    std::cout << "Running single-threaded tests..." << std::endl;

    LockFreeVector<int> vec;

    for (int i = 0; i < 100; i++) {
        vec.push_back(i);
        assert(vec.size() == i + 1);
        assert(vec.read(i) == i);
    }

    for (int i = 0; i < 100; i++) {
        int new_value = i * 2;
        vec.write(i, new_value);
        assert(vec.read(i) == new_value);
    }

    for (int i = 99; i >= 0; i--) {
        int value = vec.pop_back();
        assert(value == i * 2);
        assert(vec.size() == i);
    }

    std::cout << "Single-threaded tests passed!" << std::endl;
}

void concurrent_operations(LockFreeVector<int>& vec, int thread_id, int operations) {
    for (int i = 0; i < operations; i++) {
        int operation = generate_random(0, 2);

        switch (operation) {
            case 0: {
                vec.push_back(thread_id * 10000 + i);
                break;
            }
            case 1: {
                size_t index = generate_random(0, vec.size() - 1);
                vec.write(index, thread_id * 10000 + i);
                break;
            }
            case 2: {
                if (vec.size() > 0) {
                    size_t index = generate_random(0, vec.size() - 1);
                    vec.read(index);
                }
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(generate_random(0, 100)));
    }
}

void test_multi_threaded() {
    std::cout << "Running multi-threaded tests..." << std::endl;

    LockFreeVector<int> vec;
    const int num_threads = 4;
    const int operations_per_thread = 1000;

    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(concurrent_operations, std::ref(vec), i, operations_per_thread);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "Multi-threaded tests completed!" << std::endl;
    std::cout << "Final vector size: " << vec.size() << std::endl;
}

int main() {
    test_single_threaded();
    test_multi_threaded();

    return 0;
}