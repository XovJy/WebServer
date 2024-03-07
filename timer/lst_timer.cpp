#include "lst_timer.h"

SortTimerLst::SortTimerLst() {
    head_ = NULL;
    tail_ = NULL;
}

SortTimerLst::~SortTimerLst() {
    UtilTimer* temp = head_;
    while (temp) {
        head_ = temp->next_;
        delete temp;
        temp = head_;
    }
}

void SortTimerLst::addTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    if (!head_) {
        head_ = tail_ = timer;
        return;
    }
    if (timer->expire_ < head_->expire_) {
        timer->next_ = head_;
        head_->prev_ = timer;
        head_ = timer;
        return;
    }
    addTimer(timer, head_);
}
void SortTimerLst::addTimer(UtilTimer* timer, UtilTimer* lst_head) {
    UtilTimer* prev = lst_head;
    UtilTimer* temp = prev->next_;
    while (temp) {
        if (timer->expire_ < temp->expire_) {
            prev->next_ = timer;
            timer->next_ = temp;
            temp->prev_ = timer;
            timer->prev_ = prev;
            break;
        }
        prev = temp;
        temp = temp->next_;
    }
    if (!temp) {
        prev->next_ = timer;
        timer->prev_ = prev;
        timer->next_ = NULL;
        tail_ = timer;
    }

}
void SortTimerLst::adjustTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    UtilTimer *temp = timer->next_;
    if (!temp || (timer->expire_ < temp->expire_)) {
        return;
    }
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = NULL;
        timer->next_ = NULL;
        addTimer(timer, head_);
    } else {
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        addTimer(timer, timer->next_);
    }
}

void SortTimerLst::delTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    if ((timer == head_) && (timer == tail_)) {
        delete timer;
        head_ = NULL;
        tail_ = NULL;
        return;
    }
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = NULL;
        delete timer;
        return;
    }
    if (timer == tail_) {
        tail_ = tail_->prev_;
        tail_->next_ = NULL;
        delete timer;
        return;
    }
    timer->prev_->next_ = timer->next_;
    timer->next_->prev_ = timer->prev_;
    delete timer;
}

void SortTimerLst::tick() {
    if (!head_) {
        return;
    }

    time_t cur = time(NULL);
    UtilTimer* temp = head_;

    while (temp) {
        if (cur < temp->expire_) {
            break;
        }
        temp->cbFunc(temp->user_data_);
        head_ =temp->next_;
        if (head_) {
            head_->prev_ = NULL;
        }
        delete temp;
        temp = head_;
    }
}



void Utils::init(int times_lot) {
    m_times_slot_ = times_lot;
}

int Utils::setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epoll_fd, int fd, bool one_shot, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == trig_mode)
        event.events = EPOLLIN | EPOLLET || EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

// 信号处理函数
void Utils::signalHandler(int signal) {
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = signal;
    send(u_pipe_fd_[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addSignal(int signal, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(signal, &sa, NULL) != -1);
}

void Utils::timerHandler() {
    m_timer_lst_.tick();
    alarm(m_times_slot_);
}

void Utils::showError(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}   

int* Utils::u_pipe_fd_ = 0;
int Utils::u_epoll_fd_ = 0;

// class Utils;
void cbFunc(ClientData* user_data) {
    epoll_ctl(Utils::u_epoll_fd_, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    HttpConn::m_user_count_--;
}
