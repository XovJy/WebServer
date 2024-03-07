#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

class Sem // 信号量
{
private:
    sem_t m_sem_;
public:
    Sem() { //构造函数
        if (sem_init(&m_sem_, 0, 0) != 0) {
            throw std::exception();
        }
    }
    Sem(int num) { //构造函数
        if (sem_init(&m_sem_,0,num) != 0) {
            throw std::exception();
        }
    }
    ~Sem() { //析构函数
        sem_destroy(&m_sem_);
    }
    bool wait() {
        return sem_wait(&m_sem_) == 0;
    }
    bool post() {
        return sem_post(&m_sem_) == 0;
    }

};

class Locker // 互斥锁
{
private:
    pthread_mutex_t m_mutex_;
public:
    Locker() {
        if (pthread_mutex_init(&m_mutex_, NULL) == 0) {
            throw std::exception();
        }
    }
    ~Locker() {
        pthread_mutex_destroy(&m_mutex_);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex_) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex_) == 0;
    }
    pthread_mutex_t *get() {
        return &m_mutex_;
    }
};

class Cond { // 条件变量
private:
    pthread_cond_t m_cond_;

public:
    Cond() {
        if (pthread_cond_init(&m_cond_, NULL) != 0) {
            throw std::exception();
        }
    }

    ~Cond() {
        pthread_cond_destroy(&m_cond_);
    }
    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;

        ret = pthread_cond_wait(&m_cond_, m_mutex);

        return ret == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex_, struct timespec t) {
        int ret = 0;

        ret = pthread_cond_timedwait(&m_cond_, m_mutex_, &t);

        return ret == 0;
    }

    bool signal() {
        return pthread_cond_signal(&m_cond_) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond_) == 0;
    }
};


#endif