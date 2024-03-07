#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"
#include "../http/http_conn.h"


class UtilTimer;

struct ClientData{
    sockaddr_in address;
    int sockfd;
    UtilTimer *timer;

};

class UtilTimer{

public:
    UtilTimer() : prev_(NULL), next_(NULL) {}
public:
    time_t expire_;

    
    ClientData *user_data_;
    UtilTimer *prev_;
    UtilTimer *next_;
    void (* cbFunc)(ClientData *); //???
};

class SortTimerLst {
public:
    SortTimerLst();
    ~SortTimerLst();

    void addTimer(UtilTimer* timer);
    void adjustTimer(UtilTimer* timer);
    void delTimer(UtilTimer* timer);
    void tick();
private:
    void addTimer(UtilTimer* timer, UtilTimer* lst_head);

    UtilTimer* head_;
    UtilTimer* tail_;
};


class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int times_lot);
    // 对文件描述符设置非阻塞
    int setNonBlocking(int fd);
    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epoll_fd, int fd, bool one_shot, int trig_mode);
    // 信号处理函数
    static void signalHandler(int signal);
    // 设置信号函数
    void addSignal(int sig, void(handler)(int), bool restart = true);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timerHandler();

    void showError(int connfd, const char* info);

public:
    static int* u_pipe_fd_;
    SortTimerLst m_timer_lst_;
    static int u_epoll_fd_;
    int m_times_slot_;
};

void cbFunc(ClientData *user_data);

#endif