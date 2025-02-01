#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include "lock-free-vector.cpp"

class LockFreeVectorTest : public ::testing::Test {
protected:
    std::unique_ptr<LockFreeVector<int>> vec;

    void SetUp() override {
        vec = std::make_unique<LockFreeVector<int>>();
    }

    void TearDown() override {
        vec.reset();
    }

    static int generate_random(int min, int max) {
        static thread_local std::mt19937 generator(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(generator);
    }

    void verify_vector_contents(const std::vector<int>& expected) {
        ASSERT_EQ(vec->size(), expected.size());
        for (size_t i = 0; i < expected.size(); i++) {
            ASSERT_EQ(vec->read(i), expected[i]);
        }
    }
};

TEST_F(LockFreeVectorTest, PushBackSequential) {
    std::vector<int> expected;
    for (int i = 0; i < 100; i++) {
        vec->push_back(i);
        expected.push_back(i);
        ASSERT_EQ(vec->size(), i + 1);
        verify_vector_contents(expected);
    }
}

TEST_F(LockFreeVectorTest, PopBackSequential) {
    for (int i = 0; i < 100; i++) {
        vec->push_back(i);
    }

    for (int i = 99; i >= 0; i--) {
        ASSERT_EQ(vec->pop_back(), i);
        ASSERT_EQ(vec->size(), i);
    }
}

TEST_F(LockFreeVectorTest, ConcurrentMixedOperations) {
    const int num_threads = 4;
    const int ops_per_thread = 10000;
    std::atomic<int> total_pushes(0);
    std::atomic<int> total_pops(0);
    std::vector<std::thread> threads;

    auto mixed_work = [this, &total_pushes, &total_pops](int thread_id, int num_ops) {
        for (int i = 0; i < num_ops; i++) {
            try {
                int op = generate_random(0, 2);
                switch (op) {
                    case 0: {  // push_back
                        vec->push_back(thread_id * 10000 + i);
                        total_pushes++;
                        break;
                    }
                    case 1: {
                        if (vec->size() > 0) {
                            try {
                                vec->pop_back();
                                total_pops++;
                            } catch (const std::out_of_range&) {
                            }
                        }
                        break;
                    }
                    case 2: {
                        size_t size = vec->size();
                        if (size > 0) {
                            size_t index = generate_random(0, size - 1);
                            vec->write(index, thread_id * 10000 + i);
                        }
                        break;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Thread " << thread_id << " encountered error: "
                         << e.what() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(generate_random(0, 50)));
        }
    };

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(mixed_work, i, ops_per_thread);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(vec->size(), total_pushes - total_pops);
}