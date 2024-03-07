#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mysql/mysql.h>

#include <iostream>
#include <list>
#include <string>
#include <cstdio>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class ConnectionPool
{
public:
    MYSQL* getConnection();
    bool releaseConnection(MYSQL* conn);
    int getFreeConn();
    void destroyPool();

    //单例模式
    static ConnectionPool* getInstance();

    void init(string url, string user, string password, string database_name,
              int port, int max_conn, int close_log);

private:
    ConnectionPool(/* args */);
    ~ConnectionPool();

    int m_max_conn_;        // 最大连接数
    int m_cur_conn_;        // 当前已使用的连接数
    int m_free_conn_;       // 当前空闲的连接数
    Locker lock_;
    list<MYSQL*> conn_list_; // 连接池
    Sem reserve_;

public:
    string m_url_;          // 主机地址
    string m_port_;         // 数据库端口号
    string m_user_;         // 登陆数据库用户名
    string m_password_;     // 登陆数据库密码
    string m_database_name_;// 使用数据库名
    int m_close_log_;       // 日志开关
};

class ConnectionRAII{
public:
    ConnectionRAII(MYSQL **con, ConnectionPool *conn_pool);
    ~ConnectionRAII();

private:
    MYSQL* con_RAII_;
    ConnectionPool *pool_RAII_;
};

#endif