#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class cond {
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0)
            throw std::exception();
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    bool timewait(pthread_mutex_t* mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }
    bool signal(pthread_mutex_t* mutex) {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

class sem {
public:
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0)
        /* 
            int sem_init(sem_t *sem, int pshared, unsigned int value);
            初始化sem为value, pshared == 0 ==> 线程间共享信号量, 
            pshared == 1 ==> 进程间贡献信号量
            sem_init()  initializes the unnamed semaphore at the address
            pointed to by sem.  The value argument specifies the initial
            value for the semaphore.

            If  pshared  has  the  value 0, then the semaphore is shared
            between the threads of a process, and should be  located  at
            some  address that is visible to all threads (e.g., a global
            variable, or a variable allocated dynamically on the heap).

             If pshared is nonzero, then the semaphore is shared  between
            processes,  and should be located in a region of shared mem‐
            ory
         */
            throw std::exception();
    }
    sem(int val) {
        if (sem_init(&m_sem, 0, val) != 0)
            throw std::exception();
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        return sem_wait(&m_sem) == 0; 
        /* 
            sem > 0  --> sem--, 立即返回
            sem == 0, 阻塞, 直到sem > 0 或收到信号
            sem_wait() decrements (locks) the semaphore  pointed  to  by
            sem.   If  the  semaphore's value is greater than zero, then
            the decrement proceeds, and the  function  returns,  immedi‐
            ately.   If the semaphore currently has the value zero, then
            the call blocks until either it becomes possible to  perform
            the  decrement (i.e., the semaphore value rises above zero),
            or a signal handler interrupts the call.
         */
    }
    bool post() {
        return sem_post(&m_sem) == 0;
        /* 
            sem++, 如果++后sem > 0, 某个被sem阻塞的进程/线程将被唤醒
            sem_post()  increments (unlocks) the semaphore pointed to by
            sem.  If the semaphore's value consequently becomes  greater
            than  zero,  then  another  process  or  thread blocked in a
            sem_wait(3) call will be woken up and proceed  to  lock  the
            semaphore.
         */
    }
private:
    sem_t m_sem;
};

#endif