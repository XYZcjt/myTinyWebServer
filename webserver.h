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
#include <cassert>
#include <sys/epoll.h>

#include "threadpool/threadpool.h"
#include "http/http_conn.h"



// 最大文件描述符
const int MAX_FD=66666;
// 最大事件数量
const int MAX_EVENT_NUMBER=10000;
// 超时单位
const int TIMESLOT=5;

class webServer{
public:
    webServer();
    ~webServer();

    void init(int port, string user, string passWord, string databaseName,
            int log_write, int opt_linger, int trigmode, int sql_num,
            int thread_num, int close_log, int actor_model);
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithRead(int sockfd);
    void dealwithWrite(int sockfd);

public:
    // 数据库的一些数据，事件处理的模式，日志的开关
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    // 1为reactor模式， 0为proactor
    int m_actormodel;

    // 管道信号
    int m_pipefd[2];
    // epoll文件描述符
    int m_epollfd;
    // 客户端
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    // 优雅关闭模式是否开启
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //定时器相关
    client_data *users_timer;
    Utils utils;
};

#endif