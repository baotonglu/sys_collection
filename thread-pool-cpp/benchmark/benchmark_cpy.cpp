// #include <thread_pool.hpp>
#include <iostream>
#include <thread>
#include <cmath>
#include <chrono>

#include <cstddef>

#include <sys/mman.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <string>
#include <sstream>
#include <tuple>
#include <future>
#include <omp.h>
#include <time.h>

#include "mempool.hpp"
#include "system.hpp"

// #include "BS_thread_pool.hpp"
// #include "thread_pool.hpp"


template<typename T>
T IP(const T *a, const T *b, size_t n) {
	T ret = 0;
	for (size_t i = 0; i < n; i++) {
		ret += a[i] * b[i];
	}
	return ret;
}

class internal_test{
public:
	internal_test(MyThreadPool* pool){
		pool_ = pool;
		n = 10000;
		a = new int[n]();
		b = new int[n]();	
	}

	void test_submit(){
		size_t nr_submit = 2000;
		auto start = std::chrono::high_resolution_clock::now();
		pool_->LockQueue();
		for (size_t i = 0; i < nr_submit; i++) {
			pool_->QueueJobWOLock([this](void* param) { return this->thread_func(param); }, (void*)i);
		}
		pool_->AddNumTask(nr_submit);
		pool_->UnlockQueue();
		pool_->NotifyAll();
		pool_->Wait();
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		std::cout << "class internal thread pool(ms): " << duration/1000 << std::endl;
	}

	int thread_func(void* para){
		int sum = 0;
		// std::cout << "before exectuion, n = " <<  n << std::endl;
		for (size_t i = 0; i < n; i++) {
			sum += a[i] + b[i];
		}
		return sum;
	}

	int n;
	int *a;
	int *b;
	MyThreadPool *pool_;
};


int main () {
	// size_t nr_para = std::thread::hardware_concurrency();
	size_t nr_submit = 12;
    size_t nr_para = nr_submit;
	int n = 10000;
	int *a = new int[n]();
	int *b = new int[n]();

	auto task = [&] (int i) {
    	// std::this_thread::sleep_for(std::chrono::nanoseconds(100 * 1000)); // 100us
		// return 0;
		int sum = 0;
		for (size_t i = 0; i < n; i++) {
			sum += a[i] * b[i];
		}
		return sum;
	};

	auto mytask = [&] (void* i) {
    	// std::this_thread::sleep_for(std::chrono::nanoseconds(100 * 1000)); // 100us
		// return 0;
		int sum = 0;
		for (size_t i = 0; i < n; i++) {
			sum += a[i] * b[i];
		}
		return sum;
	};

	auto start = std::chrono::high_resolution_clock::now();
	auto ret = task(1);
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	std::cout << "ret = " << ret << std::endl;
	std::cout << "duration_per_task(ms): " << duration/1000 << std::endl;

	// tp::ThreadPool thread_pool1;
	// std::promise<int> waiters[nr_para];
	// start = std::chrono::high_resolution_clock::now();
	// for (size_t i = 0; i < nr_submit; i++) {
	// 	thread_pool1.post([i, &waiters, &task]() {
	// 			waiters[i].set_value(task(i));
	// 			});
	// }
	// for (size_t i = 0; i < nr_submit; i++) {
	// 	waiters[i].get_future().wait();
	// }
	// end = std::chrono::high_resolution_clock::now();
	// duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	// std::cout << "duration__all_submit  tp thread pool(ms): " << duration/1000 << std::endl;
	// std::cout << "Efficiency: " << duration__all_submit / duration << std::endl;

	// Below is the use case for my thread_pool
    MyThreadPool my_pool;
    my_pool.Start();
    // start = std::chrono::high_resolution_clock::now();
	// // System::profile("my_profile", [&]() {
	// 	for (size_t i = 0; i < nr_submit; i++) {
	// 		my_pool.QueueJobWOLock(mytask, (void*)i);
	// 	}
	// 	my_pool.SetNumTask(nr_submit);
	// 	my_pool.NotifyAll();
	// 	my_pool.Wait();
	// //  });
    // end = std::chrono::high_resolution_clock::now();
	// duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	// std::cout << "My duration__all_submit  tp thread pool(ms): " << duration/1000 << std::endl;

	internal_test test_class(&my_pool);
	test_class.test_submit();

	my_pool.Stop();

    // compare with the openmp
	start = std::chrono::high_resolution_clock::now();
#pragma omp parallel for num_threads(nr_submit)
	for (size_t i = 0; i < nr_submit; i++)
	    auto ret = task(1);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	std::cout << "openmp time (ms): " << duration/1000 << std::endl;

	return 0;
}

