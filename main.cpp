#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <thread>
#include <random>
#include "lock-free-vector.cpp"

using namespace std::chrono;
struct BenchmarkStats {
    double mean = 0.0;
    double median = 0.0;
    double std_dev = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::vector<double> raw_times;
    double percentile_99 = 0.0;
    double percentile_95 = 0.0;

    void calculate(std::vector<double>& times) {
        if (times.empty()) return;

        raw_times = times;
        mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

        std::sort(times.begin(), times.end());
        size_t size = times.size();
        median = (size % 2 == 0)
                 ? (times[size/2 - 1] + times[size/2]) / 2.0
                 : times[size/2];

        double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
        std_dev = std::sqrt(sq_sum / times.size() - mean * mean);

        min = times.front();
        max = times.back();

        size_t p99_index = static_cast<size_t>(times.size() * 0.99);
        size_t p95_index = static_cast<size_t>(times.size() * 0.95);
        if (p99_index >= times.size()) p99_index = times.size() - 1;
        if (p95_index >= times.size()) p95_index = times.size() - 1;

        percentile_99 = times[p99_index];
        percentile_95 = times[p95_index];
    }
};

template<typename T>
class VectorWrapper {
public:
    virtual void push_back(const T& value) = 0;
    virtual T pop_back() = 0;
    virtual void write(size_t index, const T& value) = 0;
    virtual T read(size_t index) const = 0;
    virtual size_t size() const = 0;
    virtual ~VectorWrapper() = default;
};

template<typename T>
class LockFreeVectorWrapper : public VectorWrapper<T> {
    LockFreeVector<T> vec;
public:
    void push_back(const T& value) override { vec.push_back(value); }
    T pop_back() override { return vec.pop_back(); }
    void write(size_t index, const T& value) override { vec.write(index, value); }
    T read(size_t index) const override { return vec.read(index); }
    size_t size() const override { return vec.size(); }
};

template<typename T>
class MutexVectorWrapper : public VectorWrapper<T> {
    std::vector<T> vec;
    mutable std::mutex mutex;
public:
    void push_back(const T& value) override {
        std::lock_guard<std::mutex> lock(mutex);
        vec.push_back(value);
    }

    T pop_back() override {
        std::lock_guard<std::mutex> lock(mutex);
        if (vec.empty()) throw std::out_of_range("empty");
        T value = vec.back();
        vec.pop_back();
        return value;
    }

    void write(size_t index, const T& value) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (index >= vec.size()) throw std::out_of_range("index");
        vec[index] = value;
    }

    T read(size_t index) const override {
        std::lock_guard<std::mutex> lock(mutex);
        if (index >= vec.size()) throw std::out_of_range("index");
        return vec[index];
    }

    size_t size() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return vec.size();
    }
};

void print_stats(const std::string& title, const BenchmarkStats& stats) {
    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::fixed << std::setprecision(3)
              << "Mean:       " << stats.mean << " µs\n"
              << "Median:     " << stats.median << " µs\n"
              << "StdDev:     " << stats.std_dev << " µs\n"
              << "Min:        " << stats.min << " µs\n"
              << "Max:        " << stats.max << " µs\n"
              << "99th %ile:  " << stats.percentile_99 << " µs\n"
              << "95th %ile:  " << stats.percentile_95 << " µs\n\n";
}

template<typename VectorType>
BenchmarkStats run_mixed_ops_benchmark(int num_threads, int num_runs) {
    std::vector<double> times;
    times.reserve(num_runs);

    for (int i = 0; i < 3; i++) {
        VectorType vec;
        for (int i = 0; i < 1000; ++i) vec.push_back(i);
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&vec]() {
                // Warmup operations
                for (int j = 0; j < 100; ++j) {
                    vec.push_back(j);
                }
            });
        }
        for (auto& t : threads) t.join();
    }

    for (int run = 0; run < num_runs; ++run) {
        std::unique_ptr<VectorWrapper<int>> vec = std::make_unique<VectorType>();

        for (int i = 0; i < 10000; ++i) {
            vec->push_back(i);
        }

        std::vector<std::thread> threads;
        auto start_time = high_resolution_clock::now();

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&vec]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> op_dist(0, 99);
                std::uniform_int_distribution<> val_dist(0, 1000);

                for (int op = 0; op < 100000; ++op) {
                    int operation = op_dist(gen);
                    try {
                        if (operation < 15) {
                            vec->push_back(val_dist(gen));
                        }
                        else if (operation < 20) {
                            vec->pop_back();
                        }
                        else if (operation < 30) {
                            size_t size = vec->size();
                            if (size > 0) {
                                vec->write(val_dist(gen) % size, val_dist(gen));
                            }
                        }
                        else {
                            size_t size = vec->size();
                            if (size > 0) {
                                volatile auto val = vec->read(val_dist(gen) % size);
                                (void)val;
                            }
                        }
                    }
                    catch (const std::out_of_range&) {}
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end_time - start_time).count();
        times.push_back(static_cast<double>(duration));

        //std::cout << "Run " << run + 1 << " took " << duration << " µs\n";
    }

    BenchmarkStats stats;
    stats.calculate(times);
    return stats;
}

int main() {
    const int NUM_RUNS = 25;
    std::vector<int> thread_counts = {2, 4, 6};

    std::cout << "\n=== Vector Performance Benchmark ===\n";
    std::cout << "Running " << NUM_RUNS << " iterations per configuration\n";

    for (int num_threads : thread_counts) {
        std::cout << "\nTesting with " << num_threads << " threads:\n";

        std::cout << "\nLock-Free Vector:";
        auto lockfree_stats = run_mixed_ops_benchmark<LockFreeVectorWrapper<int>>(num_threads, NUM_RUNS);
        print_stats("Lock-Free Vector Results", lockfree_stats);

        std::cout << "\nMutex Vector:";
        auto mutex_stats = run_mixed_ops_benchmark<MutexVectorWrapper<int>>(num_threads, NUM_RUNS);
        print_stats("Mutex Vector Results", mutex_stats);
    }

    return 0;
}