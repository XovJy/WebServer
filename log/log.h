#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <string>

#include "block_queue.h"

using namespace std;


class Log{
public:
    static Log* getInstance() {
        static Log instance;
        return &instance;
    }

    static void *flushLogThread(void* args) {
        Log::getInstance()->asyncWriteLog();
    }

    bool init(const char* file_name, int close_log, int log_buf_size = 8192,
              int split_lines = 5000000, int max_queue_size = 0);
    
    void writeLog(int level, const char* format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void* asyncWriteLog() {
        string single_log;

        while (m_log_queue_->pop(single_log)) {
            m_mutex_.lock();
            fputs(single_log.c_str(), m_fp_);
            m_mutex_.unlock();
        }
    }

private:
    char dir_name_[128];    // 路径名
    char log_name_[128];    // log文件名
    int m_split_lines_;      // 日志最大行数
    int m_log_buf_size_;    // 日志缓冲区大小
    long long m_count_;     // 日志行数记录
    int m_today_;           // 因为按天分类，记录当前时间是那一天
    FILE* m_fp_;            // 打开log的文件指针
    char* m_buf_;
    BlockQueue<string>* m_log_queue_;   //阻塞队列
    bool m_is_async_;        // 是否同步标志位
    Locker m_mutex_;
    int m_close_log_;       // 关闭日志
};

// TODO 
#define LOG_DEBUG(format, ...) if(0 == m_close_log_) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log_) {Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log_) {Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log_) {Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}


#endif