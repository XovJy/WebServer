#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <cassert>
#include <string>
#include <sys/epoll.h>

#include "./threadpool/thread_pool.h"
#include "./http/http_conn.h"
#include "./timer/lst_timer.h"
#include "./log/log.h"

using std::string;

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIME_SLOT = 5;            //最小超时单位



class WebServer{
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string password, string database_name,
              int log_write, int opt_linger, int trig_mode, int sql_num,
              int thread_num, int close_log, int actor_model);
    
    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjustTimer(UtilTimer *timer); //todo util_timer的定义
    void dealTimer(UtilTimer *timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool& timeout, bool& stop_server);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

public:
    int m_port_;
    char* m_root_;
    int m_log_write_;
    int m_close_log_;
    int m_actor_model_;

    int m_pipefd_[2];
    int m_epollfd_;
    HttpConn *users_; //TODO: ttpConn类的实现 HttpConn.h中的HttpConn

    //数据库相关
    ConnectionPool *m_conn_pool_; //TODO:  ConnectionPool实现
    string m_user_;
    string m_password_;
    string m_database_name_;
    int m_sql_num_;

    //线程池相关
    ThreadPool<HttpConn> *m_pool_; //TODO: TreadPool实现
    int m_thread_num_;

    //epoll_event相关
    epoll_event events_[MAX_EVENT_NUMBER];

    int m_listenfd_;
    int m_opt_linger_;
    int m_trig_mode_;
    int m_listen_trig_mode_;
    int m_conn_trig_mode_;

    //定时器相关
    ClientData *users_timer_; //TODO: ClinetData类的实现
    Utils utils_; //TODO: Utils实现

};

#endif