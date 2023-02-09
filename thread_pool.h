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
        /* 
            int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);
            在当前进程中创建并运行一个线程, 将线程ID保存到thread指向的位置
            成功返回0, 失败返回error number
            The  pthread_create() function starts a new thread in the calling
            process.  The new thread starts execution by invoking  start_rou‐
            tine(); arg is passed as the sole argument of start_routine().

            // **** critical info from chatgpt ***
            a thread created using pthread_create() does have a process control block (PCB), 
            just like a process. The PCB is an internal data structure maintained by the 
            operating system that contains information about a process or thread, such as its state, 
            priority, stack pointer, and register values.

            In the case of a thread created with pthread_create(), the operating system creates 
            a separate PCB for each thread. Each thread has its own stack and set of register values, 
            and the operating system uses the information stored in the thread's PCB to manage and 
            schedule the execution of the thread.

            The operating system uses the information stored in the PCB to manage the scheduling and 
            execution of threads, just as it does for processes. When a thread is created, the 
            operating system allocates memory for its stack and initializes the information in its 
            PCB, such as the stack pointer and register values. The operating system then schedules 
            the thread for execution and dispatches it to a processor when its turn arrives.

            Each thread has its own separate PCB, so changes to the state or register values of one 
            thread do not affect the other threads in the program. This allows threads to run 
            concurrently and independently of each other, improving performance and scalability.
        
         */
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
        /* 
            int pthread_detach(pthread_t thread);
            将thread指定的线程标记为detached, 当它终止后, 所占用的资源自动归还给系统
            The  pthread_detach()  function  marks  the  thread identified by
            thread as detached.   When  a  detached  thread  terminates,  its
            resources  are  automatically released back to the system without
            the need for another thread to join with the terminated thread.

            int pthread_join(pthread_t thread, void **retval);
            The  pthread_join()  function  waits  for the thread specified by
            thread to terminate.  If that thread has already terminated, then
            pthread_join()  returns  immediately.

            // from chatgpt, pthread_join()
            In the C programming language, pthread_join is a function that is used to 
            synchronize the execution of threads. It blocks the calling thread until 
            the specified thread terminates.

            The function pthread_join takes two arguments: the identifier of the thread 
            to be joined, and a pointer to a location where the exit status of the joined 
            thread can be stored. The exit status is a value that the thread returns when 
            it terminates.

            By using pthread_join, the calling thread can ensure that the specified thread 
            has completed its execution before it continues. This is useful when multiple 
            threads are executing concurrently and you need to wait for one thread to finish 
            before proceeding with another.

            In general, when a thread is created, it runs independently of the other threads 
            in the program. The pthread_join function allows the calling thread to wait for 
            the completion of the specified thread and obtain its exit status. This can be 
            useful for ensuring that resources are freed in the correct order, or for ensuring 
            that the results of one thread are available to another thread.
         */
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















