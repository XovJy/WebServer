#include "webserver.h"

WebServer::WebServer() {
    //HttpConn类对象
    users_ = new HttpConn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";

    m_root_ = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root_, server_path);
    strcat(m_root_, root);
    //定时器
    users_timer_ = new ClientData[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd_);
    close(m_listenfd_);
    close(m_pipefd_[1]);
    close(m_pipefd_[0]);
    delete[] users_;
    delete[] users_timer_;
    delete m_pool_;
}

void WebServer::init(int port, string user, string password, string database_name,
                int log_write, int opt_linger, int trig_mode, int sql_num,
                int thread_num, int close_log, int actor_model)
{
    m_port_ = port;
    m_user_ = user;
    m_password_ = password;
    m_database_name_ = database_name;
    m_log_write_ = log_write;
    m_opt_linger_ = opt_linger;
    m_trig_mode_ = trig_mode;
    m_sql_num_ = sql_num;
    m_thread_num_ = thread_num;
    m_close_log_ = close_log;
    m_actor_model_ = actor_model;
}


void WebServer::threadPool(){
    // 线程池
    m_pool_ = new ThreadPool<HttpConn>(m_actor_model_, m_conn_pool_, m_thread_num_);
}
void WebServer::sqlPool(){
    // 初始化数据库连接池
    m_conn_pool_ = ConnectionPool::getInstance();
    m_conn_pool_->init("localhost", m_user_, m_password_, m_database_name_, 3306, m_sql_num_, m_close_log_);

    // 初始化数据库读取表
    users_->initMysqlResult(m_conn_pool_);
}

void WebServer::logWrite(){
    if (0 == m_close_log_) {
        //初始化日志
        if (1 == m_log_write_) {
            Log::getInstance()->init("./ServerLog", m_close_log_, 2000, 800000, 800);
        } else {
            Log::getInstance()->init("./ServerLog", m_close_log_, 2000, 800000, 0);
        }
        
    }
}
void WebServer::trigMode(){
    //LT + LT
    if (0 == m_trig_mode_) {
        m_listen_trig_mode_ = 0;
        m_conn_trig_mode_ = 0;
    }
    //LT + ET
    else if (1 == m_trig_mode_) {
        m_listen_trig_mode_ = 0;
        m_conn_trig_mode_ = 1;
    } 
    //ET + LT
    else if (2 == m_trig_mode_) {
        m_listen_trig_mode_ = 1;
        m_conn_trig_mode_ = 0;
    } 
    //ET + ET
    else if (3 == m_trig_mode_) {
        m_listen_trig_mode_ = 1;
        m_conn_trig_mode_ = 1;
    }
}


void WebServer::eventListen(){

    m_listenfd_ = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd_ >= 0);

    // 优雅关闭连接
    if (0 == m_opt_linger_) {
        struct linger temp = {0, 1};
        setsockopt(m_listenfd_, SOL_SOCKET, SO_LINGER, &temp, sizeof(temp));
    } else if (1 == m_opt_linger_) {
        struct linger temp = {1, 1};
        setsockopt(m_listenfd_, SOL_SOCKET, SO_LINGER, &temp, sizeof(temp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port_);

    int flag = 1;
    setsockopt(m_listenfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd_, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd_, 5);
    assert(ret >= 0);

    utils_.init(TIME_SLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd_ = epoll_create(5);
    assert(m_epollfd_ != -1);

    utils_.addfd(m_epollfd_, m_listenfd_, false, m_listen_trig_mode_);
    HttpConn::m_epollfd_ = m_epollfd_;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd_);
    assert(ret != -1);
    utils_.setNonBlocking(m_pipefd_[1]);
    utils_.addfd(m_epollfd_, m_pipefd_[0], false, 0);

    utils_.addSignal(SIGPIPE, SIG_IGN);
    utils_.addSignal(SIGALRM, utils_.signalHandler, false);
    utils_.addSignal(SIGTERM, utils_.signalHandler, false);

    alarm(TIME_SLOT);

    // 工具类，信号和描述符基础操作
    Utils::u_pipe_fd_ = m_pipefd_;
    Utils::u_epoll_fd_ = m_epollfd_;

}

void WebServer::eventLoop(){
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd_, events_, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = events_[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd_) {
                bool flag = dealClientData();
                if (false == flag) 
                    continue;
            } else if (events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器关闭连接，移除对应的定时器
                UtilTimer* timer = users_timer_[sockfd].timer;
                dealTimer(timer, sockfd);
            } else if ((sockfd == m_pipefd_[0]) && (events_[i].events & EPOLLIN)) {
                // 处理信号
                bool flag = dealWithSignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            } else if (events_[i].events & EPOLLIN) {
                dealWithRead(sockfd);
            } else if (events_[i].events & EPOLLOUT) {
                dealWithWrite(sockfd);
            }
        }
        if (timeout) {
            utils_.timerHandler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}

void WebServer::timer(int connfd, struct sockaddr_in client_address){
    users_[connfd].init(connfd, client_address, m_root_, m_conn_trig_mode_, m_close_log_, m_user_, m_password_, m_database_name_);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer_[connfd].address = client_address;
    users_timer_[connfd].sockfd = connfd;
    UtilTimer* timer = new UtilTimer;
    timer->user_data_ = &users_timer_[connfd];
    timer->cbFunc = cbFunc;
    time_t cur = time(NULL);
    timer->expire_ = cur + 3 * TIME_SLOT;
    users_timer_[connfd].timer = timer;
    utils_.m_timer_lst_.addTimer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer *timer) {
    time_t cur = time(NULL);
    timer->expire_ = cur + 3 * TIME_SLOT;
    utils_.m_timer_lst_.adjustTimer(timer);

    LOG_INFO("%s", "adjust timer once");
}
void WebServer::dealTimer(UtilTimer *timer, int sockfd){
    timer->cbFunc(&users_timer_[sockfd]);
    if (timer) {
        utils_.m_timer_lst_.delTimer(timer);
    }

    LOG_INFO("close fd %d", users_timer_[sockfd].sockfd);
}

bool WebServer::dealClientData(){
    struct sockaddr_in client_address;
    socklen_t client_addr_length = sizeof(client_address);
    if (0 == m_listen_trig_mode_) {
        int connfd = accept(m_listenfd_, (struct sockaddr*)&client_address, &client_addr_length);
        if (connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (HttpConn::m_user_count_ >= MAX_FD) {
            utils_.showError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);

    } else {

        while (1) {
            int connfd = accept(m_listenfd_, (struct sockaddr*)&client_address, &client_addr_length);
            if (connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HttpConn::m_user_count_ >= MAX_FD) {
                utils_.showError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;

    }
    return true;
}
bool WebServer::dealWithSignal(bool& timeout, bool& stop_server){
    int ret = 0;
    int signal;
    char signals[1024];
    ret = recv(m_pipefd_[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    } else if (ret == 0) {
        return false;
    } else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM: 
            {
                stop_server = true;
                break;    
            }
            default:
                break;
            }
        }
    }
    return true;
}
void WebServer::dealWithRead(int sockfd){
    UtilTimer* timer = users_timer_[sockfd].timer;

    // reactor
    if (1 == m_actor_model_) {
        if (timer) {
            adjustTimer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool_->append(users_ + sockfd, 0);

        while (true) {
            if (1 == users_[sockfd].improv_) {
                if (1 == users_[sockfd].timer_flag_) {
                    dealTimer(timer, sockfd);
                    users_[sockfd].timer_flag_ = 0;
                }
                users_[sockfd].improv_ = 0;
                break;
            }
        }
    } else {
        // proactor
        if (users_[sockfd].readOnce()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users_[sockfd].getAddress()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            m_pool_->append_p(users_ + sockfd);

            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
    
}
void WebServer::dealWithWrite(int sockfd){
    UtilTimer* timer = users_timer_[sockfd].timer;
    //reactor
    if (1 == m_actor_model_) {
        if (timer) {
            adjustTimer(timer);
        }
        m_pool_->append(users_ + sockfd, 1);

        while (true) {
            if (1 == users_[sockfd].timer_flag_) {
                dealTimer(timer, sockfd);
                users_[sockfd].timer_flag_ = 0;
            }
            users_[sockfd].improv_ = 0;
            break;
        }
    } else {
        // proactor
        if (users_[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users_[sockfd].getAddress()->sin_addr));

            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}


