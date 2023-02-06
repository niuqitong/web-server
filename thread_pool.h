#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>
#include "locker.h"

template<typename T>
class thread_pool {
public:
    thread_pool(int thread_number = 8, int max_requests = 10000);
    ~thread_pool();
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();

    int m_thread_number; // # of threads in the pool
    pthread_t* m_threads; // container of the threads

    // request queue, linked list
    int m_max_requests;
    std::list<T*> m_work_queue;

    locker m_queue_locker;

    // sem
    sem m_queue_stat;

    // whether to end the thread
    bool m_stop;

};

template<typename T>
thread_pool<T>::thread_pool(int thread_number, int max_requests) : 
    m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(NULL) {
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    for (int i = 0; i < thread_number; i++) {
        printf("creating %d-th thread\n", i);
        
        // in c++, worker has to be a static function静态函数
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();  
        }
    }
}

template<typename T>
thread_pool<T>::~thread_pool() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool thread_pool<T>::append(T* request) {
    m_queue_locker.lock();
    if (m_work_queue.size() > m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }
    m_work_queue.push_back(request); // std::list<http_connection*> m_work_queue;
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true; 
}

template<typename T>
void* thread_pool<T>::worker(void* arg) {
    thread_pool* pool = (thread_pool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void thread_pool<T>::run() {
    while (!m_stop) {
        m_queue_stat.wait(); // sem m_queue_stat;
        m_queue_locker.lock(); // pthread_mutex_t 
        if (m_work_queue.empty()) { // std::list<T*> m_work_queue;
            m_queue_locker.unlock();
            continue;
        }
        T* request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queue_locker.unlock();
        if (!request)
            continue;
        request->process();
    }
}
#endif















