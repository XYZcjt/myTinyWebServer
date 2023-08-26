#include "webserver.h"

webServer::webServer(){
    // 创建MAX_FD个http类对象
    users = new http_conn[MAX_FD];
    
    // root文件夹路径
    char server_path[200];

    // getcwd用于获取当前工作目录的路径名
    getcwd(server_path, 200);
    
    printf("服务器路径: %s\n",server_path);

    char root[6]="/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root)+1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    printf("root文件夹的路径为：%s\n", m_root);

    // 定时器
    users_timer=new client_data[MAX_FD];
}

webServer::~webServer(){
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void webServer::init(int port,string user,string passWord,string databaseName,
            int log_write,int opt_linger,int trigmode,int sql_num,
            int thread_num,int close_log,int actor_model){
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

// 分别设置监听和连接的事件触发模式
void webServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

// void webServer::log_write()
// {
//     if (0 == m_close_log)
//     {
//         //初始化日志
//         if (1 == m_log_write)
//             Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
//         else
//             Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
//     }
// }

void webServer::sql_pool(){
    // 初始化数据库连接池
    m_connPool=connection_pool::GetInstance();
    m_connPool->init("localhost", m_user ,m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void webServer::thread_pool(){
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num, 10000);
}

void webServer::eventListen(){
    m_listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(m_listenfd>=0);

    // 优雅关闭连接
    if(m_OPT_LINGER==0){
        /*
        struct linger {
            int l_onoff;    // 1: 启用 SO_LINGER; 0: 禁用
            int l_linger;   // 在关闭连接前的等待时间（秒）
        };
        */
        struct linger tmp={0,1};
        // SOL_SOCKET 表示套接字级别选项
        // SO_LINGER用于控制套接字在关闭连接时的行为,  它指定在关闭连接时是否等待未发送的数据发送完毕，以及在关闭连接后等待多长时间。
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }
    else if(m_OPT_LINGER==1){
        struct linger tmp={1,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    int ret=0;
    struct sockaddr_in address;
    // 等同于memset(&address, 0, sizeof(address));
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    // htonl 用于将 32 位整数从主机字节序,转换为网络字节序(小端转大端)
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(m_port);

    // flag 被设置为 1 的目的是将 SO_REUSEADDR 套接字选项启用。在大多数系统中，将 SO_REUSEADDR 选项设置为非零值（通常为 1）表示启用这个选项，从而允许地址重用。
    int flag = 1;
    // SO_REUSEADDR 是一个套接字选项，用于控制在关闭套接字后，能否立即重用绑定的地址
    setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));

    ret=bind(m_listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret>=0);

    ret=listen(m_listenfd,5);
    assert(ret>=0);

    // 初始化超时时间
    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // m_LISTENTrigmode的值代表是否使用ET
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // 创建一个双向通信的管道，可以在两个进程之间进行通信
    // m_pipefd 是一个长度为 2 的整型数组，用于存储创建的管道的文件描述符
    // socketpair 函数将创建两个套接字，它们的文件描述符将存储在 m_pipefd[0] 和 m_pipefd[1] 中
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    // 将管道的写入端设置为非阻塞模式
    utils.setnonblocking(m_pipefd[1]);

    // 将管道的读取端添加到 epoll 实例中
    // 第三个参数表示是否设置为 ET 模式（这里为 false，表示使用 LT 模式），第四个参数为事件处理模式。
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 这行代码将信号 SIGPIPE 的处理方式设置为忽略
    // SIGPIPE 信号通常在写入已关闭的 socket 连接时触发，如果不处理此信号，进程可能会终止。
    // 通过将其设置为忽略，可以避免进程因为写入已关闭的 socket 而终止。
    utils.addsig(SIGPIPE, SIG_IGN);
    
    // 将信号 SIGALRM 的处理方式设置为调用 utils 对象的 sig_handler 函数。
    // 第三个参数 false 表示不使用 SA_RESTART 标志，在信号处理函数返回后不会重新启动系统调用。
    // 由alarm系统调用产生timer时钟信号
    utils.addsig(SIGALRM, utils.sig_handler, false);
    // 终端发送的终止信号
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // 唤醒
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void webServer::timer(int connfd, struct sockaddr_in client_address){
    // 把数据出入http中并且初始化
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    
    // 设置定时器相关数据
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;

    // 将定时器存储在webServer维护的users_timer数组中
    users_timer[connfd].timer = timer;
    // 将定时器添加在链表中
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void webServer::adjust_timer(util_timer *timer){
    time_t cur = time(NULL);
    timer->expire = cur + 3*TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    printf("%s\n","adjust timer once!");
}

void webServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    printf("close fd %d. \n", users_timer[sockfd].sockfd);
}

bool webServer::dealclinetdata(){
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    //  m_LISTENTrigmode为 0 是LT触发模式
    if(m_LISTENTrigmode == 0){
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd < 0){
            printf("%s:errno is:%d", "accept error", errno);
            return false;
        }
        // 用户超过了服务器极限
        if(http_conn::m_user_count >= MAX_FD){
            utils.show_error(connfd, "Internal server busy");
            printf("%s", "Internal server busy");
            return false;
        }
        // 给指定的用户添加定时器
        timer(connfd, client_address);
    }
    // ET触发模式
    else{
        while (true)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                printf("%s:errno is:%d \n", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                printf("%s\n", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

 // 接收到特定信号时更新服务器状态
bool webServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    //  使用 recv 从管道的读端 m_pipefd[0] 接收信号数据。signals 数组将用于存储接收到的数据
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    // 接收失败
    if (ret == -1){
        return false;
    }
    // 没有数据接收
    else if (ret == 0){
        return false;
    }
    else{
        for (int i = 0; i < ret; ++i){
            switch (signals[i]){
                // 有发送过来的 SIGALR 信号，表示要处理超时的定时器
                case SIGALRM:{
                    timeout = true;
                    break;
                }
                // 表示需要停止服务器
                // SIGTERM表示在终端使用Ctrl+c，要停止当前服务器
                case SIGTERM:{
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void webServer::dealwithRead(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel){
        if (timer){
            // 有数据传入，将超时时间往后拖
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true){
            if (1 == users[sockfd].improv){
                if (1 == users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        //proactor
        if (users[sockfd].read_once())
        {
            // LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            // printf("deal with the client!! \n");
            printf("deal with the client(%s)\n", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer){
                adjust_timer(timer);
            }
        }
        else{
            deal_timer(timer, sockfd);
        }
    }
}

void webServer::dealwithWrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel){
        if (timer){
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true){
            if (1 == users[sockfd].improv){
                if (1 == users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        //proactor
        if (users[sockfd].write()){
            // LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            printf("send data to the client(%s)\n", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer){
                adjust_timer(timer);
            }
        }
        else{
            deal_timer(timer, sockfd);
        }
    }
}

void webServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server){
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // EINTR 表示 "Interrupted System Call"，即系统调用被中断。
        // 它通常在程序通过信号（比如 Ctrl+C）中断阻塞的系统调用时发生。这种情况下，系统调用可能没有完成它的操作，而被信号中断。
        if (number < 0 && errno != EINTR){
            printf("%s\n", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd){
                // 正常网络通信读取用户发来的数据，然后加入定时器
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            // 处理异常事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
                // 处理管道写端传过来的信号
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    printf("%s\n", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN){
                dealwithRead(sockfd);
            }
            else if (events[i].events & EPOLLOUT){
                dealwithWrite(sockfd);
            }
        }
        // timeout为 true时，定时处理超时的定时器
        if (timeout){
            utils.timer_handler();

            printf("%s\n", "timer tick");

            timeout = false;
        }
    }
}