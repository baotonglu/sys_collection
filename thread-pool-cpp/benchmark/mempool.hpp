#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring> // for memcpy
#include <ctime>
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

// Thread pool
// Note that this thread pool should be reused across multiple layers
class MyThreadPool {
public:
    void Start(uint32_t num_threads = 0);
    // void QueueJob(const std::function<void()>& job, uint32_t);
    void QueueJobWOLock(const std::function<void(uint32_t)>& job, uint32_t para);
    void NotifyAll();
    void NotifyOne();
    void Stop();
    void Wait();
    void SetNumTask(uint32_t);
    void NotifyMain();

private:
    void ThreadLoop(uint32_t); // input is the thread id

    bool should_terminate = false;           // Tells threads to stop looking for jobs
    std::mutex queue_mutex;                  // Prevents data races to the job queue
    std::mutex main_mutex;
    std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
    std::condition_variable main_condition; // main thread uses this condition variable to wait
    std::vector<std::thread> threads;
    std::queue<std::pair<std::function<void(uint32_t)>,uint32_t>> jobs;
    std::atomic<uint32_t> num_tasks;
    bool notify_main_thread;
};

void MyThreadPool::Start(uint32_t num_threads) {
    if(num_threads == 0){
        num_threads = std::thread::hardware_concurrency();
    }

    for (uint32_t ii = 0; ii < num_threads; ++ii) {
        threads.emplace_back(std::thread(&MyThreadPool::ThreadLoop, this, ii));
    }

    num_tasks = 0;
    notify_main_thread = false;
}

void MyThreadPool::ThreadLoop(uint32_t thread_idx) {
    set_affinity(thread_idx); 
    while (true) {
        std::pair<std::function<void(uint32_t)>,uint32_t> job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            mutex_condition.wait(lock, [this] {
                return !jobs.empty() || should_terminate;
            });
            if (should_terminate) {
                return;
            }
            job = jobs.front();
            jobs.pop();
        }
        job.first(job.second); // Store the result in some places?
        num_tasks--;
        if(num_tasks == 0 && notify_main_thread == true){
            std::unique_lock<std::mutex> lock(main_mutex);
            main_condition.notify_one();
            notify_main_thread = false;
        }
    }
}

void MyThreadPool::QueueJobWOLock(const std::function<void(uint32_t)>& job, uint32_t para) {
    jobs.push(std::pair<std::function<void(uint32_t)>,uint32_t>(job, para));
}

void MyThreadPool::SetNumTask(uint32_t num){
    num_tasks = num;
}

void MyThreadPool::NotifyMain(){
    notify_main_thread = true;
}

void MyThreadPool::NotifyAll() {
    mutex_condition.notify_all();
}

void MyThreadPool::NotifyOne() {
    mutex_condition.notify_one();
}

// Only use this when the system terminates
void MyThreadPool::Stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        should_terminate = true;
    }
    mutex_condition.notify_all();
    for (std::thread& active_thread : threads) {
        active_thread.join();
    }
    threads.clear();
}

// Wait until all submitted tasks have been executed
void MyThreadPool::Wait(){
    {
        std::unique_lock<std::mutex> lock(main_mutex);
        if(num_tasks == 0) return;
        main_condition.wait(lock, [this] {
            return num_tasks == 0;
        });
    }
}