#include <thread_pool.hpp>
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

int
main () {
	// size_t nr_para = std::thread::hardware_concurrency();
	size_t nr_submit = 12;
    size_t nr_para = nr_submit;

	auto task = [] (uint32_t i) {
    	std::this_thread::sleep_for(std::chrono::nanoseconds(100 * 1000)); // 100us
		return 0;
	};

	auto start = std::chrono::high_resolution_clock::now();
	auto ret = task(1);
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	std::cout << "duration_per_task(ms): " << duration/1000 << std::endl;

	// double duration__all_submit = duration * (std::ceil((double)nr_submit / (double)nr_para));
	// std::cout << "duration__all_submit theoretically(ms): " << duration__all_submit/1000 << std::endl;

	// start = std::chrono::high_resolution_clock::now();
	// for (size_t i = 0; i < nr_submit; i++)
	//     auto ret = task(1);
	// end = std::chrono::high_resolution_clock::now();
	// auto duration_32 = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	// std::cout << "duration 32 task(ms): " << duration_32/1000 << std::endl;


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
    start = std::chrono::high_resolution_clock::now();
	System::profile("my_profile", [&]() {
		for (size_t i = 0; i < nr_submit; i++) {
			my_pool.QueueJobWOLock(task, i);
		}
		my_pool.SetNumTask(nr_submit);
		my_pool.NotifyMain();
		my_pool.NotifyAll();
		my_pool.Wait();
	 });
    end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

	std::cout << "My duration__all_submit  tp thread pool(ms): " << duration/1000 << std::endl;
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

