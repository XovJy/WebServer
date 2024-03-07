#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H


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
#include <map>
#include <string>
#include <mysql/mysql.h>


#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


using std::map;
using std::string;
using std::fstream;

class HttpConn{

public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpConn() {}
    ~HttpConn() {}

public:
    void init(int sockfd, const sockaddr_in& addr, char*, int, int,
              string user, string password, string sql_name);
    void closeConn(bool real_close = true);
    void process();
    bool readOnce();
    bool write();
    sockaddr_in *getAddress() { return &m_address_;};
    void initMysqlResult(ConnectionPool *conn_pool);
    int timer_flag_;
    int improv_;


private:
    void init();
    HTTP_CODE processRead();
    bool processWrite(HTTP_CODE ret);
    HTTP_CODE parseRequestLine(char* text);
    HTTP_CODE parseHeaders(char* text);
    HTTP_CODE parseContent(char* text);
    LINE_STATUS parseLine();
    HTTP_CODE doRequest();
    char* getLine() { return m_read_buf_ + m_start_line_; };
    void unmap();
    bool addResponse(const char* format, ...); // ???
    bool addContent(const char* content);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int content_len);
    bool addContentType();
    bool addContentLength(int content_len);
    bool addLinger();
    bool addBlankLine();

public:
    static int m_epollfd_;
    static int m_user_count_;
    MYSQL *mysql_;
    int m_state_; //读为0, 写为1

private:
    int m_sockfd_;
    sockaddr_in m_address_;
    char m_read_buf_[READ_BUFFER_SIZE];
    long m_read_idx_;
    long m_checked_idx_;
    int m_start_line_;
    char m_write_buf_[WRITE_BUFFER_SIZE];
    int m_write_idx_;
    CHECK_STATE m_check_state_;
    METHOD m_method_;
    char m_real_file_[FILENAME_LEN];
    char* m_url_;
    char* m_version_;
    char* m_host_;
    long m_content_length_;
    bool m_linger_;
    char* m_file_address_;
    struct stat m_file_stat_;
    struct iovec m_iv_[2];
    int m_iv_count_;
    int cgi_;           //是否启用的POST
    char* m_string_;    //存储请求头数据
    int bytes_to_send_;
    int bytes_have_send_;
    char* doc_root_;

    map<string, string> m_users_;
    int m_trig_mode_;
    int m_close_log_;

    char sql_user_[100];
    char sql_password_[100];
    char sql_name_[100];

};

#endif