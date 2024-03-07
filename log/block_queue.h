/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <iostream>

#include "../lock/locker.h"

using namespace std;

template <class T>
class BlockQueue {
public:
    BlockQueue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }

        m_max_size_ = max_size;
        m_array_ = new T[max_size];
        m_size_ = 0;
        m_front_ = -1;
        m_back_ = -1;
    }
    ~BlockQueue() {
        m_mutex_.lock();
        if (m_array_ != NULL)
            delete[] m_array_;
        m_mutex_.unlock();
    }

    void clear() {
        m_mutex_.lock();
        m_size_ = 0;
        m_front_ = -1;
        m_back_ = -1;
        m_mutex_.unlock();
    }

    bool full() {
        m_mutex_.lock();
        if (m_size_ >= m_max_size_) {
            m_mutex_.unlock();
            return true;
        }
        m_mutex_.unlock();
        return false;
    }

    bool empty() {
        m_mutex_.lock();
        if (0 == m_size_) {
            m_mutex_.unlock();
            return true;
        }
        m_mutex_.unlock();
        return false;
    }

    bool front(T& value) {
        m_mutex_.lock();
        if (0 == m_size_) {
            m_mutex_.unlock();
            return false;
        }
        value = m_array_[m_front_];
        m_mutex_.unlock();
        return true;
    }

    bool back(T& value) {
        m_mutex_.lock();
        if (0 == m_size_) {
            m_mutex_.unlock();
            return false;
        }
        value = m_array_[m_back_];
        m_mutex_.unlock();
        return true;
    }

    int size() {
        int temp = 0;

        m_mutex_.lock();
        temp = m_size_;
        m_mutex_.unlock();

        return temp;
    }

    int maxSize() {
        int temp = 0;

        m_mutex_.lock();
        temp = m_max_size_;
        m_mutex_.unlock();

        return temp;
    }

    bool push(const T& item) {
        m_mutex_.lock();
        if (m_size_ >= m_max_size_) {
            m_cond_.broadcast();
            m_mutex_.unlock();
            return false;
        }

        m_back_ = (m_back_ + 1) % m_max_size_;
        m_array_[m_back_] = item;

        m_size_++;

        m_cond_.broadcast();
        m_mutex_.unlock();
        return true;
    }

    bool pop(T& item) {
        m_mutex_.lock();
        // ?
        while (m_size_ <= 0) {
            if (!m_cond_.wait(m_mutex_.get())) {
                m_mutex_.unlock();
                return false;
            }
        }
        m_front_ = (m_front_ + 1) % m_max_size_;
        item = m_array_[m_front_];
        m_size_--;
        m_mutex_.unlock();
        return true;
    }

    bool pop(T& item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex_.lock();
        if (m_size_ <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond_.timewait(m_mutex_.get(), t)) {
                m_mutex_.unlock();
                return false;
            }
        }

        if (m_size_ <= 0) {
            m_mutex_.unlock();
            return false;
        }

        m_front_ = (m_front_ + 1) % m_max_size_;
        item = m_array_[m_front_];
        m_size_--;
        m_mutex_.unlock();
        return true;
    }


private:
    Locker m_mutex_;
    Cond m_cond_;

    T* m_array_;
    int m_size_;
    int m_max_size_;
    int m_front_;
    int m_back_;

};





#endif