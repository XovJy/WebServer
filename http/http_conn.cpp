#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

#include "../log/log.h"


const char* okk_200_title = "OKK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "404 Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

Locker m_lock;
map<string, string> users;


// 将SQL信息存储到map中
void HttpConn::initMysqlResult(ConnectionPool *conn_pool) {
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    ConnectionRAII mysqlconn(&mysql, conn_pool);

    // 在user表中检索username，password，浏览器端输入
    if (mysql_query(mysql, "SELECT username, password FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    // 从表中检索完整的结果
    MYSQL_RES* result = mysql_store_result(mysql);
    
    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // 从结果集合中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞 
int setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件注册表读事件，ET模式，选择开启EPOLLONESHOT 
void addfd(int epoll_fd, int fd, bool one_shot, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == trig_mode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    } else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}


// 从内核时间表删除描述符 
void removefd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


// 将事件重置为EPOLLONESHOT 
void modfd(int epoll_fd, int fd, int ev, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;
    
    if (1 == trig_mode) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::m_user_count_ = 0;
int HttpConn::m_epollfd_ = -1;



void HttpConn::init(int sockfd, const sockaddr_in& addr, char* root, int trig_mode,
                    int close_log, string user, string passwd, string sql_name) {
    m_sockfd_ = sockfd;
    m_address_ = addr;

    addfd(m_epollfd_, sockfd, true, m_trig_mode_);
    m_user_count_++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或者htpp相应格式出错或者访问的文件中内容完全为空
    doc_root_ = root;
    m_trig_mode_ = trig_mode;
    m_close_log_ = close_log;

    strcpy(sql_user_, user.c_str());
    strcpy(sql_password_, passwd.c_str());
    strcpy(sql_name_, sql_name.c_str());

    init();
}

// 初始化新接受的连接 
// check_state默认为分析请求行状态
void HttpConn::init() {
    mysql_ = NULL;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    m_check_state_ = CHECK_STATE_REQUESTLINE;
    m_linger_ = false;
    m_method_ = GET;
    m_url_ = 0;
    m_version_ = 0;
    m_content_length_ = 0;
    m_host_ = 0;
    m_start_line_ = 0;
    m_checked_idx_ = 0;
    m_read_idx_ = 0;
    m_write_idx_ = 0;
    cgi_ = 0;
    m_state_ = 0;
    timer_flag_ = 0;
    improv_ = 0;

    memset(m_read_buf_, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file_, '\0', FILENAME_LEN);
}


// 关闭连接，关闭一个连接，客户总量减1
void HttpConn::closeConn(bool real_close) {
    if (real_close && (m_sockfd_ != -1)) {
        printf("close %d\n", m_sockfd_);
        removefd(m_epollfd_, m_sockfd_);
        m_sockfd_ = -1;
        m_user_count_--;
    }
}

//
void HttpConn::process() {
    HTTP_CODE read_ret = processRead();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd_, m_sockfd_, EPOLLIN, m_trig_mode_);
        return;
    }
    bool write_ret = processWrite(read_ret);
    if (!write_ret) {
        closeConn();
    }
    modfd(m_epollfd_, m_sockfd_, EPOLLOUT, m_trig_mode_);
} 

bool HttpConn::readOnce() {
    if (m_read_idx_ >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据 
    if (0 == m_trig_mode_) {
        bytes_read = recv(m_sockfd_, m_read_buf_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_, 0);
        m_read_idx_ += bytes_read;

        if (bytes_read <= 0) {
            return false;
        }
        return true;
    } else { // ET读数据
        while (true) {
            bytes_read = recv(m_sockfd_, m_read_buf_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if (bytes_read == 0) {
                return false;
            }
            m_read_idx_ += bytes_read;
            return true;
        }
    }
    return false;
}

bool HttpConn::write(){
    int temp = 0;

    if (bytes_to_send_ == 0) {
        modfd(m_epollfd_, m_sockfd_, EPOLLIN, m_trig_mode_);
        init();
        return true;
    }

    while (1) {
        temp = writev(m_sockfd_, m_iv_, m_iv_count_);

        if (temp < 0) {
            if (errno == EAGAIN) {
                modfd(m_epollfd_, m_sockfd_, EPOLLOUT, m_trig_mode_);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;
        if (bytes_have_send_ >= m_iv_[0].iov_len) {
            m_iv_[0].iov_len = 0;
            m_iv_[1].iov_base = m_file_address_ + (bytes_have_send_ - m_write_idx_);
            m_iv_[1].iov_len = bytes_to_send_;
        } else {
            m_iv_[0].iov_base = m_write_buf_ + bytes_have_send_;
            m_iv_[0].iov_len = m_iv_[0].iov_len - bytes_have_send_;
        }

        if (bytes_to_send_ <= 0) {
            unmap();
            modfd(m_epollfd_, m_sockfd_, EPOLLIN, m_trig_mode_);

            if (m_linger_) {
                init();
                return true;
            } else {
                return false;
            }
        }

    }
}


HttpConn::HTTP_CODE HttpConn::processRead() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while ((m_check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parseLine()) == LINE_OK)) {
        text = getLine();
        m_start_line_ = m_checked_idx_;
        LOG_INFO("%s", text);
        switch (m_check_state_) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                    return doRequest();
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST)
                    return doRequest();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }      
    }
    return NO_REQUEST;
}

bool HttpConn::processWrite(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (!addContent(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST: {
            addStatusLine(404, error_400_title);
            addHeaders(strlen(error_404_form));
            if (!addContent(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addContent(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST: {
            addStatusLine(200, okk_200_title);
            if (m_file_stat_.st_size != 0) {
                addHeaders(m_file_stat_.st_size);
                m_iv_[0].iov_base = m_write_buf_;
                m_iv_[0].iov_len = m_write_idx_;
                m_iv_[1].iov_base = m_file_address_;
                m_iv_[1].iov_len = m_file_stat_.st_size;
                m_iv_count_ = 2;
                bytes_to_send_ = m_write_idx_ = m_file_stat_.st_size;
                return true;
            } else {
                const char* ok_string = "<html><body></body></html>";
                addHeaders(strlen(ok_string));
                if (!addContent(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv_[0].iov_base = m_write_buf_;
    m_iv_[0].iov_len = m_write_idx_;
    m_iv_count_ = 1;
    bytes_to_send_ = m_write_idx_;
    return true;
}

// 解析http请求行，获得请求方法，目标url和Http版本号
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char* text) {
    m_url_ = strpbrk(text, " \t");
    if (!m_url_)
        return BAD_REQUEST;
    *m_url_++ = '\0';
    char* method = text;

    // 目前只实现GET和POST
    if (strcasecmp(method, "GET") == 0)
        m_method_ = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method_ = POST;
        cgi_ = 1;
    } else 
        return BAD_REQUEST;

    m_url_ += strspn(m_url_, " \t");
    m_version_ = strpbrk(m_url_, " \t");
    if (!m_version_) 
        return BAD_REQUEST;
    *m_version_++ = '\0';
    m_version_ += strspn(m_version_, " \t");

    if (strcasecmp(m_version_, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url_, "http://", 7) == 0) {
        m_url_ += 7;
        m_url_ = strchr(m_url_, '/');
    }

    if (strncasecmp(m_url_, "https://", 8) == 0) {
        m_url_ += 8;
        m_url_ = strchr(m_url_, '/');
    }

    if (!m_url_ || m_url_[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示判断界面
    if (strlen(m_url_) == 1)
        strcat(m_url_, "judge.html");
    m_check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


HttpConn::HTTP_CODE HttpConn::parseHeaders(char* text) {
    if (text[0] == '\0') {
        if (m_content_length_ != 0) {
            m_check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "Keep-alive") == 0) {
            m_linger_ = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length_ = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host_ = text;
    } else {
        LOG_INFO("Oop! Unknow Header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
HttpConn::HTTP_CODE HttpConn::parseContent(char* text) {
    if (m_read_idx_ >= (m_content_length_ + m_checked_idx_)) {
        text[m_content_length_] = '\0';
        //POST//请求中最后为输入的用户名和密码
        m_string_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK, LINE_BAD,LINE_OPEN
HttpConn::LINE_STATUS HttpConn::parseLine() {
    char temp;
    for (;m_checked_idx_ < m_read_idx_; ++m_checked_idx_) {
        temp = m_read_buf_[m_checked_idx_];
        if (temp == '\r') {
            if ((m_checked_idx_ + 1) == m_read_idx_)
                return LINE_OPEN;
            else if (m_read_buf_[m_checked_idx_ + 1] == '\n') {
                m_read_buf_[m_checked_idx_++] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx_ > 1 && m_read_buf_[m_checked_idx_ - 1] == '\r') {
                m_read_buf_[m_checked_idx_ - 1] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;

}


HttpConn::HTTP_CODE HttpConn::doRequest() {
    strcpy(m_real_file_, doc_root_);
    int len = strlen(doc_root_);

    const char* p = strrchr(m_url_, '/');

    // 处理cgi
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {

        // 根据标志位判断是登陆检测还是注册检测
        char flag = m_url_[1];

        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, m_url_ + 2);
        strncpy(m_real_file_ + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string_[i] != '&'; ++i)
            name[i - 5] = m_string_[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string_[i] != '\0'; ++i, ++j)
            password[j] = m_string_[i];
        password[j] = '\0';

        if (*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql_, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url_, "/log.html");
                else 
                    strcpy(m_url_, "/registerError.html");
            } else {
                strcpy(m_url_, "/registerError.html");
            }
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url_, "/welcome.html");
            else 
                strcpy(m_url_, "/logError.html");
        }
    } 

    if (*(p + 1) == '0')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        strncpy(m_real_file_ + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/log.html");
        strncpy(m_real_file_ + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/picture.html");
        strncpy(m_real_file_ + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/video.html");
        strncpy(m_real_file_ + len, url_real, strlen(url_real));

        free(url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/fans.html");
        strncpy(m_real_file_ + len, url_real, strlen(url_real));

        free(url_real);
    }
    else
        strncpy(m_real_file_ + len, m_url_, FILENAME_LEN - len - 1);

    if (stat(m_real_file_, &m_file_stat_) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat_.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat_.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file_, O_RDONLY);
    m_file_address_ = (char*)mmap(0, m_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConn::unmap() {
    if (m_file_address_) {
        munmap(m_file_address_, m_file_stat_.st_size);
        m_file_address_ = 0;
    }
}

bool HttpConn::addResponse(const char* format, ...) {
    if (m_write_idx_ > WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf_ + m_write_idx_, WRITE_BUFFER_SIZE - 1 - m_write_idx_, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx_)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx_ += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf_);
    return true;
} // ???

bool HttpConn::addContent(const char* content) {
    return addResponse("%s", "\r\n");
}

bool HttpConn::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::addHeaders(int content_len) {
    return addContentLength(content_len) && addLinger() && addBlankLine();
}
bool HttpConn::addContentType() {
    return addResponse("Content-Type:%s\r\n", "text/html");
}
bool HttpConn::addContentLength(int content_len) {
    return addResponse("Content-Length:%d\r\n", content_len);
}
bool HttpConn::addLinger() {
    return addResponse("Connection:%s\r\n", (m_linger_ == true) ? "keep-alive" : "close");
}
bool HttpConn::addBlankLine() {
    return addResponse("%s", "\r\n");
}