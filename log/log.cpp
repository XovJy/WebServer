#include "log.h"

Log::Log() {
    m_count_ = 0;
    m_is_async_ = false;
}

Log::~Log() {
    if (m_fp_ != NULL) {
        fclose(m_fp_);
    }
}

bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1) {
        m_is_async_ = true;
        m_log_queue_ = new BlockQueue<string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flushLogThread, NULL);
    }

    m_close_log_ = close_log;
    m_log_buf_size_ = log_buf_size;
    m_buf_ = new char[m_log_buf_size_];
    memset(m_buf_, '\0', m_log_buf_size_);
    m_split_lines_ = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", 
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1, 
                 my_tm.tm_mday, file_name);
    } else {
        strcpy(log_name_, p + 1);
        strncpy(dir_name_, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", 
                 dir_name_, my_tm.tm_year + 1900, my_tm.tm_mon + 1, 
                 my_tm.tm_mday, log_name_);
    }

    m_today_ = my_tm.tm_mday;

    m_fp_ = fopen(log_full_name, "a");
    if (m_fp_ == NULL) {
        return false;
    }
    return true;
}

void Log::writeLog(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 写入一个log，对m_count++, m_split_lines最大行数
    m_mutex_.lock();
    m_count_++;

    
    if (m_today_ != my_tm.tm_mday || m_count_ % m_split_lines_ == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(m_fp_);
        fclose(m_fp_);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if (m_today_ != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name_, tail, log_name_);
            m_today_ = my_tm.tm_mday;
            m_count_ = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name_, tail, log_name_, m_count_ / m_split_lines_);
        }
        m_fp_ = fopen(new_log, "a");
    }

    m_mutex_.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex_.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf_ + n, m_log_buf_size_ - n - 1, format, valst);
    m_buf_[n + m] = '\n';
    m_buf_[n + m + 1] = '\0';
    log_str = m_buf_;

    m_mutex_.unlock();

    if (m_is_async_ && !m_log_queue_->full())
    {
        m_log_queue_->push(log_str);
    }
    else
    {
        m_mutex_.lock();
        fputs(log_str.c_str(), m_fp_);
        m_mutex_.unlock();
    }

    va_end(valst);
}

void Log::flush(void) {
    m_mutex_.lock();
    // 强制刷新写入流缓冲区
    fflush(m_fp_);
    m_mutex_.unlock();
}