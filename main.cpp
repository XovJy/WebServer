#include "config.h"
#include <string>

using std::string;


int main(int argc, char* argv[]) {

    // 需要修改的数据库信息
    string user = "root";
    string password = "12345678";
    string database_name = "testdb";
    printf("hello is ok\n");
    Config config;
    config.ParseArg(argc, argv);

    WebServer server;
    
    //初始化
    server.init(config.port_, user, password, database_name, config.log_write_,
                config.opt_linger_, config.trig_mode_, config.sql_num_, config.thread_num_,
                config.close_log_, config.actor_model_);
    
    printf("init is ok\n");
    //日志
    server.logWrite();

    //数据库
    server.sqlPool();

    //线程池
    server.threadPool();

    //触发模式
    server.trigMode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;

}