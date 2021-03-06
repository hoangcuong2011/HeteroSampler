#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "utils.h"
#include <thread>
#include <chrono>
#include <condition_variable>

static long getFingerPrint(long iterations, long startSeed) { // random hash function taken from 6.816.
  const long m = (long) 0xFFFFFFFFFFFFL;
  const long a = 25214903917L;
  const long c = 11L;
  long seed = startSeed;
  for(long i = 0; i < iterations; i++) {
    seed = (seed*a + c) & m;
  }
  return ( seed >> 12 );
}

// ThreadPool using consumer-producer model.
// each thread has a unique id, RNG and log.
// type of work is T.
template<class T>
class ThreadPool {
public:
  // constructor.
  ThreadPool(size_t num_threads, std::function<void(int, const T&)> worker);
  ~ThreadPool();
  // return number of threads in the pool.
  size_t numThreads() const {return this->th.size(); }
  // add work (type T) to the thread pool.
  void addWork(const T& work);
  // wait a quescent moment when there is no active work.
  void waitFinish();
  // lock.
  void lock();
  // unlock.
  void unlock();
  std::function<void(int, const T&)> worker;

  std::vector<objcokus> rngs;
private:
  void initThreads(size_t num_threads);
  std::vector<std::shared_ptr<std::thread> > th;
  std::list<T> th_work;
  size_t active_work;
  std::mutex th_mutex;
  std::condition_variable th_cv, th_finished;
  std::vector<std::shared_ptr<std::stringstream> > th_stream;
  std::vector<std::shared_ptr<XMLlog> > th_log;

  bool is_stopped;
};


template<class T>
ThreadPool<T>::ThreadPool(size_t num_threads, std::function<void(int, const T&)> worker)
:worker(worker), is_stopped(false) {
  this->initThreads(num_threads);
}

template<class T>
ThreadPool<T>::~ThreadPool() {
  std::unique_lock<std::mutex> lock(th_mutex);
  is_stopped = true;
  active_work = th.size();
  lock.unlock(); 
  th_cv.notify_all();
  waitFinish();
  for(std::shared_ptr<std::thread>& t : this->th) {
    t->detach();  // must detach before delete according to c++ 11.
    t = nullptr;
  }
}

template<class T>
void ThreadPool<T>::initThreads(size_t num_threads) {
  th.resize(num_threads);
  this->rngs.resize(num_threads);
  active_work = 0;
  for(size_t ni = 0; ni < num_threads; ni++) {
    this->rngs[ni].seedMT(getFingerPrint(10, ni+1));
    this->th_stream.push_back(std::shared_ptr<std::stringstream>(new std::stringstream()));
    this->th_log.push_back(std::shared_ptr<XMLlog>(new XMLlog(*th_stream.back())));
    this->th[ni] = std::shared_ptr<std::thread>(new std::thread([&] (int tid) {
      std::unique_lock<std::mutex> lock(th_mutex);
      while(true) {
	if(is_stopped) {
	  active_work-=1;
	  th_finished.notify_all();
	  lock.unlock();
	  return;
	}
	if(th_work.size() > 0) {
	  T work = th_work.front();
	  th_work.pop_front();
	  active_work = active_work+1;
	  lock.unlock();
	  th_stream[tid]->str("");
	  worker(tid,  work);
	  lock.lock();
	  active_work = active_work-1;
	  th_finished.notify_all();
	}else{
	  th_cv.wait(lock);	
	}
      }
    }, ni));
  }
}

template<class T>
void ThreadPool<T>::addWork(const T& work) {
  std::unique_lock<std::mutex> lock(th_mutex);
  this->th_work.push_back(work); 
  lock.unlock(); 
  th_cv.notify_all();
}

template<class T>
void ThreadPool<T>::waitFinish() {
  std::unique_lock<std::mutex> lock(th_mutex);
  while(true) {
    if(active_work + th_work.size() == 0) break;
    th_finished.wait(lock);
  }
  lock.unlock();
}

template<class T>
void ThreadPool<T>::lock() {
  th_mutex.lock();
}

template<class T>
void ThreadPool<T>::unlock() {
  th_mutex.unlock();
}
#endif
