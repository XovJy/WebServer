#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


template <typename T>
class ThreadPool
{
public:
    /* thread_number是线程池中线程的数量，max_request是请求队列中最多允许的、等待处理的请求数量 */
    ThreadPool(int actor_model, ConnectionPool *conn_pool, int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /* 工作线程运行的函数，它不断从工作队列中取出任务并执行 */
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number_;        // 线程池中的线程数
    int m_max_requests_;         // 请求队列中允许的最大请求数
    pthread_t *m_threads_;       // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue_; // 请求队列
    Locker m_queue_locker_;      // 保护请求队列的互斥锁
    Sem m_queue_stat_;           // 是否有任务需要处理
    ConnectionPool *m_conn_pool_;// 数据库
    int m_actor_model_;          // 模型切换 reactor proactor
};

                

/* 类中函数的实现 */

/* 构造函数 */
template <typename T>
ThreadPool<T>::ThreadPool(int actor_model, ConnectionPool *conn_pool, int thread_number, int max_requests)
: m_actor_model_(actor_model), m_thread_number_(thread_number), m_max_requests_(max_requests), m_threads_(NULL), m_conn_pool_(conn_pool) 
{
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    m_threads_ = new pthread_t[m_thread_number_];
    if (!m_threads_) {
        throw std::exception();
    }
    for (int i = 0; i < thread_number; ++i) {
        if (pthread_create(m_threads_ + i, NULL, worker, this) != 0) {
            delete[] m_threads_;
            throw std::exception();
        }
        if (pthread_detach(m_threads_[i])) {
            delete[] m_threads_;
            throw std::exception();
        }

    }
}

/* 析构函数 */
template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads_;
}



template <typename T>
bool ThreadPool<T>::append(T *request, int state) {
    m_queue_locker_.lock();
    if (m_workqueue_.size() >= m_max_requests_) {
        m_queue_locker_.unlock();
        return false;
    }
    request->m_state_ = state; // request的定义
    m_workqueue_.push_back(request);
    m_queue_locker_.unlock();
    m_queue_stat_.post();
    return true;
}

template <typename T>
bool ThreadPool<T>::append_p(T *request) {
    m_queue_locker_.lock();
    if (m_workqueue_.size() >= m_max_requests_) {
        m_queue_locker_.unlock();
        return false;
    }
    m_workqueue_.push_back(request);
    m_queue_locker_.unlock();
    m_queue_stat_.post();
    return true;    
}

template <typename T>
void *ThreadPool<T>::worker(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void ThreadPool<T>::run() {
    while (true) {
        m_queue_stat_.wait();
        m_queue_locker_.lock();
        if (m_workqueue_.empty()) {
            m_queue_locker_.unlock();
            continue;
        }

        T *request = m_workqueue_.front();
        m_workqueue_.pop_front();
        m_queue_locker_.unlock();
        if (!request) {
            continue;
        }
        if (1 == m_actor_model_) {
            if (0 == request->m_state_) {
                if(request->readOnce()) {
                    request->improv_ = 1;
                    ConnectionRAII mysqlcon(&request->mysql_, m_conn_pool_);
                    request->process();
                } else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            } else {
                if (request->write()) {
                    request->improv_ = 1;
                } else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
        } else {
            ConnectionRAII mysqlcon(&request->mysql_, m_conn_pool_);
            request->process();
        }
    }
}
#endif