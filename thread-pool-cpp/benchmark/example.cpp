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
#include <time.h>
#include "mempool.hpp"

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

	// Below is the example of nested submit
	void subtask(){
		size_t nr_submit = 2000;
		pool_->LockQueue();
		for (size_t i = 0; i < nr_submit; i++) {
			pool_->QueueJobWOLock([this](void* param) { return this->thread_func(param); }, (void*)i);
		}
		pool_->AddNumTask(nr_submit);
		pool_->UnlockQueue();
		pool_->NotifyAll();
	}

	void nested_submit(){
		auto start = std::chrono::high_resolution_clock::now();
		pool_->LockQueue();
		pool_->QueueJobWOLock([this](void* param) { return this->subtask(); }, nullptr);
		pool_->AddNumTask(1);
		pool_->UnlockQueue();
		pool_->NotifyAll();
		pool_->Wait();
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		std::cout << "nested thread pool(ms): " << duration/1000 << std::endl;
	}

	int thread_func(void* para){
		int sum = 0;
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
    MyThreadPool my_pool;
    my_pool.Start();
	
	internal_test test_class(&my_pool);
	test_class.test_submit();
	test_class.nested_submit();
	
	my_pool.DisplayNumTask();
	my_pool.Stop();
	return 0;
}

