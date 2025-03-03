#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring> // for memcpy
#include <ctime>
#include <omp.h>
#include <mutex>
#include <condition_variable>
#include <immintrin.h>
#include <sched.h>
#include <algorithm>
#include <immintrin.h>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <utility>
#include <atomic>
// #include <ctpl.h>

const size_t GB = 1024 * 1024 * 1024;
const size_t MB = 1024 * 1024;
const size_t KB = 1024;
const size_t BUFFER_SIZE = 4 * MB;
const size_t CHUNK_SIZE = 4 * KB;
const size_t element_num_in_chunk = CHUNK_SIZE / 8;
const size_t ORG_ARRAY_SIZE = 1 * GB;

int bar_a, bar_b, bar_c;
std::mutex mtx;
std::condition_variable cv;
bool finished;
 
// ADD and SUB return the value after add or sub
#define ADD(_p, _v) (__atomic_add_fetch(_p, _v, __ATOMIC_SEQ_CST))
#define SUB(_p, _v) (__atomic_sub_fetch(_p, _v, __ATOMIC_SEQ_CST))
#define LOAD(_p) (__atomic_load_n(_p, __ATOMIC_SEQ_CST))
#define STORE(_p, _v) (__atomic_store_n(_p, _v, __ATOMIC_SEQ_CST))

void set_affinity(uint32_t idx) {
    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(idx, &my_set);
    // if (idx < 12) {
    //   CPU_SET(idx * 2, &my_set);
    // //   std::cout << "Bind thread to CPU " << idx * 2 << std::endl;
    // } else {
    //   CPU_SET((idx - 12) * 2 + 1, &my_set);
    // //   std::cout << "Bind thread to CPU " << idx << std::endl;
    // }
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
}

struct task_tag{
    task_tag(){
        active = 0;
    }

    std::function<void(uint32_t)> job;
    uint32_t idx;
    std::atomic<uint64_t> active; // 1 means activate, 0 means inactive
    uint64_t padding[2];
};

// Thread pool
class ThreadPool {
public:
    void Start(uint32_t num_threads);
    // void QueueJob(const std::function<void()>& job, uint32_t);
    void QueueJob(std::function<void(uint32_t)> job, uint32_t);
    void QueueJobWOLock(std::function<void(uint32_t)> job, uint32_t pos_idx, uint32_t thread_idx);
    void NotifyAll();
    void Stop();
    bool Busy();
    void SetNumTask(int num);

private:
    void ThreadLoop(uint32_t);

    bool should_terminate = false;           // Tells threads to stop looking for jobs
    std::mutex queue_mutex;                  // Prevents data races to the job queue
    std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
    std::vector<std::thread> threads;
    std::queue<std::pair<std::function<void(uint32_t)>,uint32_t>> jobs;
    std::atomic<int> num_run_threads;
    task_tag* task_array;
    std::atomic<int> num_tasks;
    std::condition_variable* condition_array;
};

void ThreadPool::Start(uint32_t num_threads) {
    // const uint32_t num_threads = std::thread::hardware_concurrency(); // Max # of threads the system supports
    for (uint32_t ii = 0; ii < num_threads; ++ii) {
        threads.emplace_back(std::thread(&ThreadPool::ThreadLoop, this, ii));
    }
    num_run_threads = 0;
    task_array = new task_tag[num_threads];
    condition_array = new std::condition_variable[num_threads];
    // for(uint32_t i = 0; i < num_threads; ++i){
    //     task_array[i].active = 0;
    // }
    num_tasks = 0;
}

void ThreadPool::ThreadLoop(uint32_t thread_idx) {
    set_affinity(thread_idx); // bind thread to the CPU core
    // std::cout << "Thread " << thread_idx << " is looping" << std::endl;
    while (true) {
        // std::pair<std::function<void(uint32_t)>, uint32_t> job;
        // {
        //     std::unique_lock<std::mutex> lock(queue_mutex);
        //     mutex_condition.wait(lock, [this] {
        //         return !jobs.empty() || should_terminate;
        //     });
        //     if (should_terminate) {
        //         return;
        //     }
        //     job = jobs.front();
        //     jobs.pop();
        //     num_run_threads++;
        // }
        // job.first(job.second);
        // num_run_threads--;

        if (should_terminate) {
            return;
        }

        if(task_array[thread_idx].active == 1){
            // std::cout << "thread " << thread_idx << " start taking tasks" << std::endl;
            task_array[thread_idx].job(task_array[thread_idx].idx);
            task_array[thread_idx].active = 0;
            num_tasks--;
            // std::cout << "thread " << thread_idx << " finish current tasks" << std::endl;
            // std::cout << "num_tasks = " << num_tasks << std::endl;
        }
    }
}

void ThreadPool::QueueJob(std::function<void(uint32_t)> job, uint32_t idx) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        jobs.push(std::pair<std::function<void(uint32_t)>,uint32_t>(job, idx));
    }
    mutex_condition.notify_one();
}

void ThreadPool::QueueJobWOLock(std::function<void(uint32_t)> job, uint32_t pos_idx, uint32_t thread_idx) {
    // jobs.push(std::pair<std::function<void(uint32_t)>,uint32_t>(job, pos_idx));
    // std::cout << "Enqueue job to thread " << thread_idx << std::endl;
    task_array[thread_idx].job = job;
    task_array[thread_idx].idx = pos_idx;
    task_array[thread_idx].active = 1;
    num_tasks++;
}

void ThreadPool::SetNumTask(int num){
    num_tasks = num;
}

void ThreadPool::NotifyAll() {
    mutex_condition.notify_all();
}

bool ThreadPool::Busy() {
    bool poolbusy;
    // {
        // std::unique_lock<std::mutex> lock(queue_mutex);
        // poolbusy = (!jobs.empty()) || (num_run_threads != 0) ;
    poolbusy = (num_tasks != 0);
    // }
    return poolbusy;
}

void ThreadPool::Stop() {
    // {
    //     std::unique_lock<std::mutex> lock(queue_mutex);
    //     should_terminate = true;
    // }
    should_terminate = true;
    // mutex_condition.notify_all();
    for (std::thread& active_thread : threads) {
        active_thread.join();
    }
    threads.clear();
}

struct record_time{
    uint64_t time_count;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
    uint64_t padding[5];
};

record_time time_array[24];

// How to create CPU threads in advance and use it as the thread pool
struct chunk_array{
    public: 
    chunk_array(){
        std::srand(static_cast<unsigned>(std::time(0)));
        for (int i = 0; i < element_num_in_chunk; ++i){
            data[i] = std::rand();
        }
    }

    uint64_t data[element_num_in_chunk];
};

inline void spin_wait() {
    SUB(&bar_b, 1);
    while (LOAD(&bar_a) == 1)
      ; /*spinning*/
}

inline void end_notify() {
    if (SUB(&bar_c, 1) == 0) {
        std::unique_lock<std::mutex> lck(mtx);
        finished = true;
        cv.notify_one();
    }
}
  
inline void end_sub() { SUB(&bar_c, 1); }
  
void execute_memcpy(chunk_array* dst_chunk_array_, chunk_array* org_chunk_array_, size_t* copy_index, size_t num_copy_chunk, size_t start_index){
    set_affinity(start_index / num_copy_chunk);
    spin_wait();
    auto start_time = std::chrono::high_resolution_clock::now();

    for(size_t i = start_index; i < start_index + num_copy_chunk; ++i){
        std::memcpy(dst_chunk_array_[i].data, org_chunk_array_[copy_index[i]].data, CHUNK_SIZE);
        // _avx_cpy_unroll(dst_chunk_array_[i].data, org_chunk_array_[copy_index[i]].data, CHUNK_SIZE);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    time_array[start_index / num_copy_chunk].time_count = duration;
    if(start_index / num_copy_chunk == 0){
        std::cout << "One thread latency = " << duration << " microseconds" << std::endl;
    }
    end_notify();
}

void analyze_time(size_t num){
    uint64_t min_time = time_array[0].time_count; 
    uint64_t max_time = time_array[0].time_count; 
    uint64_t sum_time = time_array[0].time_count;
    auto first_time = time_array[0].start_time;
    auto end_time = time_array[0].end_time;

    std::cout << "Latency (ms) for thread 0 = " << sum_time << std::endl;

    for(size_t i = 1; i < num; ++i){
        sum_time += time_array[i].time_count;
        min_time = std::min(min_time, time_array[i].time_count);
        max_time = std::max(max_time, time_array[i].time_count);
        first_time = std::min(first_time, time_array[i].start_time);
        end_time = std::max(end_time, time_array[i].end_time);
        std::cout << "Latency (ms) for thread " << i << " = " << time_array[i].time_count << std::endl;
    }

    std::cout << "Avg time (ms) = " << sum_time / num << "; min time (ms) = " << min_time << "; max time (ms) = " << max_time << std::endl;
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - first_time).count();
    std::cout << "Total time during the thread exectuion is " << duration << " ms" << std::endl;
}

void test_memcpy_latency(size_t num_threads) {
    ThreadPool mypool;
    mypool.Start(num_threads);

    size_t num_total_chunk = ORG_ARRAY_SIZE / CHUNK_SIZE;
    // void* ptr = nullptr;
    // int result = posix_memalign(&ptr, 128, num_total_chunk * sizeof(chunk_array));
    // if (result != 0) {
    //     std::cerr << "Memory allocation failed: " << strerror(result) << std::endl;
    //     return;
    // }
    // chunk_array *org_chunk_array_ = reinterpret_cast<chunk_array*>(ptr);
    chunk_array* org_chunk_array_ = new chunk_array[num_total_chunk];
    std::cout << "There are " << num_total_chunk << " blocks in the original buffer." << std::endl;

    // Generate index for accesses in the original sets
    size_t num_copy_chunk = BUFFER_SIZE / CHUNK_SIZE;
    std::srand(static_cast<unsigned>(std::time(0)));
    size_t* copy_index = new size_t[num_copy_chunk]();
    for(int i = 0; i < num_copy_chunk; ++i){
        copy_index[i] = std::rand() % num_total_chunk;
    }

    chunk_array* dst_chunk_array_ = new chunk_array[num_copy_chunk];
    // result = posix_memalign(&ptr, 128, num_copy_chunk * sizeof(chunk_array));
    // if (result != 0) {
    //     std::cerr << "Memory allocation failed: " << strerror(result) << std::endl;
    //     return;
    // }

    // chunk_array *dst_chunk_array_ = reinterpret_cast<chunk_array*>(ptr);

    // Pre-test some latency numbers
    // auto start_time = std::chrono::high_resolution_clock::now();

    // for(size_t i = 0; i < num_copy_chunk; ++i){
    //     // if(i < 10){
    //     //     std::cout << "Current copy index " << i << ": " << copy_index[i] << std::endl;
    //     // }

    //     // if(i + 10 > num_copy_chunk){
    //     //     std::cout << "Current copy index " << i << ": " << copy_index[i] << std::endl;
    //     // }

    //     std::memcpy(dst_chunk_array_[i].data, org_chunk_array_[copy_index[i]].data, CHUNK_SIZE);
    // }

    // auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    // std::cout << "Pre-test thread latency = " << duration << " microseconds" << std::endl;

    // For multi-thread control
    // Use thread pool for multi-threading copy

    uint32_t num_copy_chunk_per_thread = num_copy_chunk /  num_threads;
    std::function<void(uint32_t)> pure_memcpy = [num_copy_chunk_per_thread, dst_chunk_array_, org_chunk_array_, copy_index](uint32_t start_index){
        auto start_time = std::chrono::high_resolution_clock::now();
        for(uint32_t i = start_index; i < start_index + num_copy_chunk_per_thread; ++i){
            std::memcpy(dst_chunk_array_[i].data, org_chunk_array_[copy_index[i]].data, CHUNK_SIZE);
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        auto arr_idx = start_index / num_copy_chunk_per_thread;
        time_array[arr_idx].start_time = start_time;
        time_array[arr_idx].end_time = end_time;
        time_array[arr_idx].time_count = duration;
    };

    // std::cout << "prepare submit jobs" << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();

    for(uint32_t i = 0; i < num_threads; ++i){
        // mypool.QueueJob(pure_memcpy, num_copy_chunk_per_thread * i);
        // std::cout << "Submit Job " << i << std::endl;
        mypool.QueueJobWOLock(pure_memcpy, num_copy_chunk_per_thread * i, i);
    }

    // mypool.SetNumTask(num_threads);
    // mypool.NotifyAll();
    // std::cout << "main thread is waiting" << std::endl;
    while(mypool.Busy()){
        // sleep for ms
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // Need to sleep on a condition variable
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    // bar_a = 1;
    // bar_b = num_threads;
    // bar_c = num_threads;
    // finished = false;

    // std::thread *thread_array[1024];
    // size_t num_copy_chunk_per_thread = num_copy_chunk / num_threads;
    // for(size_t i = 0; i < num_threads; ++i){
    //     thread_array[i] = new std::thread(execute_memcpy, dst_chunk_array_, org_chunk_array_, copy_index, num_copy_chunk_per_thread, num_copy_chunk_per_thread * i);
    // }

    // while (LOAD(&bar_b) != 0)
    // ;                                     // Spin
    // std::unique_lock<std::mutex> lck(mtx);  // get the lock of condition variable

    // auto start_time = std::chrono::high_resolution_clock::now();
    // STORE(&bar_a, 0);  // start test
    // while (!finished) {
    //     cv.wait(lck);  // go to sleep and wait for the wake-up from child threads
    // }

    // auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    // for (int i = 0; i < num_threads; ++i) {
    //     thread_array[i]->join();
    // }

    analyze_time(num_threads);
    std::cout << "Memcpy latency with " << num_threads << " threads: " << duration << " microseconds" << std::endl;

    mypool.Stop();

    // for (int i = 0; i < num_threads; ++i) {
    //     delete thread_array[i];
    // }

    // delete [] dst_chunk_array_;
    // delete [] org_chunk_array_;
    std::free(dst_chunk_array_);
    std::free(org_chunk_array_);
    delete [] copy_index;
}

void test_different_copy_latency(){
    size_t num_total_chunk = ORG_ARRAY_SIZE / CHUNK_SIZE;
    std::cout << "There are " << num_total_chunk << " blocks in the original buffer." << std::endl;

    // Generate index for accesses in the original sets
    size_t buffer_size_array[13] = {4 * KB, 8 * KB, 16 * KB, 32 * KB, 64 * KB, 128 * KB, 256 * KB, 512 * KB, 1 * MB, 2 * MB, 4 * MB, 8 * MB, 16 * MB};

    for(size_t i = 0; i < 13; ++i){
        chunk_array* org_chunk_array_ = new chunk_array[num_total_chunk]();
        size_t num_copy_chunk = buffer_size_array[12 - i] / CHUNK_SIZE;
        std::srand(static_cast<unsigned>(std::time(0)));
        size_t* copy_index = new size_t[num_copy_chunk]();
        for(int i = 0; i < num_copy_chunk; ++i){
            copy_index[i] = std::rand() % num_total_chunk;
        }
    
        chunk_array* dst_chunk_array_ = new chunk_array[num_copy_chunk];

        auto start_time = std::chrono::high_resolution_clock::now();
        for(size_t i = 0; i < num_copy_chunk; ++i){
            std::memcpy(dst_chunk_array_[i].data, org_chunk_array_[copy_index[i]].data, CHUNK_SIZE);
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        delete [] dst_chunk_array_;
        delete [] org_chunk_array_;
        std::cout << "Memcpy latency for buffer = " << buffer_size_array[12 - i] / KB << " KB: " << duration << " microseconds" << std::endl;
    }
}

int main() {
    std::cout << "Function size = " << sizeof(std::function<void(uint32_t)>) << std::endl;
    std::cout << "Task tag size = " << sizeof(task_tag) << std::endl;
    std::cout << "Size of record time = " << sizeof(record_time) << std::endl;

    size_t thread_array[6] = {1, 2, 4, 8, 12, 24};

    for (int i = 0; i < 6; ++i) {
        test_memcpy_latency(thread_array[i]);
    }

    // test_memcpy_latency(1);
    // test_different_copy_latency();
    return 0;
}