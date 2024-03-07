#include "sql_connection_pool.h"

ConnectionPool::ConnectionPool() {
    m_cur_conn_ = 0;
    m_free_conn_ = 0;
}

ConnectionPool* ConnectionPool::getInstance() {
    static ConnectionPool conn_pool;
    return &conn_pool;
}

// 构造初始化
void ConnectionPool::init(string url, string user, string password, string database_name,
              int port, int max_conn, int close_log) {

    m_url_ = url;
    m_port_ = port;
    m_user_ = user;
    m_password_ = password;
    m_database_name_ = database_name;
    m_close_log_ = close_log;

    for (int i = 0; i < max_conn; i++) {
        MYSQL* conn = NULL;
        conn = mysql_init(conn);

        if (conn == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), 
                                  database_name.c_str(), port, NULL, 0);

        if (conn == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        conn_list_.push_back(conn);
        ++m_free_conn_;
    }
    reserve_ = Sem(m_free_conn_);

    m_max_conn_ = m_free_conn_;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* ConnectionPool::getConnection() {
    MYSQL* conn = NULL;

    if (0 == conn_list_.size())
        return NULL;
    reserve_.wait();

    lock_.lock();

    conn = conn_list_.front();
    conn_list_.pop_front();

    --m_free_conn_;
    ++m_cur_conn_;

    lock_.unlock();
    return conn;
}


// 释放当前使用连接
bool ConnectionPool::releaseConnection(MYSQL* conn) {
    if (NULL == conn)
        return false;
    lock_.lock();

    conn_list_.push_back(conn);
    ++m_free_conn_;
    --m_cur_conn_;

    lock_.unlock();

    reserve_.post();
    return true;
}

// 销毁数据库连接池
void ConnectionPool::destroyPool() {
    lock_.lock();
    if (conn_list_.size() > 0) {
        list<MYSQL*>::iterator iter;
        for (iter = conn_list_.begin(); iter != conn_list_.end(); ++iter) {
            MYSQL* conn = *iter;
            mysql_close(conn);
        }
        m_cur_conn_ = 0;
        m_free_conn_ = 0;
        conn_list_.clear();
    }
    lock_.unlock();
}

// 当前空闲的连接数
int ConnectionPool::getFreeConn() {
    return this->m_free_conn_;
}

ConnectionPool::~ConnectionPool() {
    destroyPool();
}
ConnectionRAII::ConnectionRAII(MYSQL** SQL, ConnectionPool* conn_pool) {
    *SQL = conn_pool->getConnection();

    con_RAII_ = *SQL;
    pool_RAII_ = conn_pool;
    
}

ConnectionRAII::~ConnectionRAII() {
    pool_RAII_->releaseConnection(con_RAII_);
}