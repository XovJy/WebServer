#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

class Config {
public:
    Config();
    ~Config() {};

    void ParseArg(int argc, char* argv[]);

    // 端口号
    int port_;

    //日志写入方式
    int log_write_;

    //触发组合模式
    int trig_mode_;

    //listenfd触发模式
    int listen_trig_mode_;

    //connfd触发模式
    int conn_trig_mode_;

    //优雅关闭链接
    int opt_linger_;

    //数据库连接池数量
    int sql_num_;

    //线程池内的线程数量
    int thread_num_;

    //是否关闭日志
    int close_log_;

    //并发模型选择
    int actor_model_;

};

#endif