#include "lst_timer.h"
#include "../http/http_conn.h"

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

// 初始化
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

// 销毁链表
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 添加定时器
void sort_timer_lst::add_timer(util_timer *timer){
    if(!timer){
        return;
    }
    if(!head){
        head = timer;
        tail = timer;
        return;
    }

    // 新的定时器的超时时间小于头结点
    // 直接将当前的定时器作为头节点
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 以上情况都不成立，调用私有add_timer，在内部查找
    add_timer(timer, head);
}

// 调整定时器，任务发生变化时，调整定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer *timer){
    if(!timer){
        return;
    }
    util_timer *tmp = timer->next;
    // (!tmp)被调整的定时器在尾部
    // (timer->expire < tmp->expire)定时器超时值小于后继超时器
    if(!tmp || (timer->expire < tmp->expire)){
        return;
    }
    // 被调整的定时器是链表头结点，取出定时器，重新插入
    if(head == timer){
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 被调整的定时器在内部
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer){
    if(!timer){
        return;
    }
    // 只有一个定时器，直接删除
    if((timer == head) && (timer == tail)){
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 删除的定时器为头结点
    if(head == timer){
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 删除的定时器为尾结点
    if(tail == timer){
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 内部删除
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 定时任务处理函数，主循环调用一次处理函数，就处理链表容器中到期的定时器
void sort_timer_lst::tick(){
    if(!head){
        return;
    }

    // 获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;
    // 遍历容器列表
    while (tmp)
    {
        // 链表升序排列，当前时间小于超时时间，后面定时器没有到期
        if(cur < tmp->expire){
            break;
        }
        // 执行回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);
        // 将处理后的定时器从链表容器中删除，并重置头结点
        head = tmp->next;
        if(head){
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer,util_timer *lst_head){
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    // 遍历当前结点之后的链表，找到相应的为位置
    while(tmp){
        if(timer->expire < tmp->expire){
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 在前面没有匹配的配置，插入尾部
    if(!tmp){
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot){
    m_TIMESLOT = timeslot;
}

// 对文件描述符设置非阻塞
int Utils::setnonblocking(int fd){
    // fcntl控制文件描述符属性
    // F_GETFL获取文件状态的标志
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数
void Utils::sig_handler(int sig){
    // 为保证函数的可重入性，保留原理的errno
    int save_errno = errno;
    int msg = sig;
    // 使用send函数将msg的字节表示发送到管道的写端（u_pipefd[1]）
    send(u_pipefd[1], (char*)&msg, 1, 0);
    // 通过恢复原始的 errno 值，确保函数不会影响其他部分的代码对 errno 的使用
    errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void (handler)(int), bool restart){
    /*
    struct sigaction {
        void     (*sa_handler)(int); // 指向信号处理函数的指针
        void     (*sa_sigaction)(int, siginfo_t *, void *); // 扩展的信号处理函数
        sigset_t   sa_mask; // 额外的信号集，在信号处理函数执行期间被阻塞
        int        sa_flags; // 一些标志位，控制信号处理行为
        void     (*sa_restorer)(void); // 仅在特定情况下使用，已被废弃
    };
    */
    struct sigaction sa;
    // 清空初始化sa
    memset(&sa, '\0', sizeof(sa));
    // 设置信号处理函数
    sa.sa_handler = handler;
    if(restart){
        // 设置 SA_RESTART 标志，表示系统调用在信号处理返回时重新启动
        sa.sa_flags |= SA_RESTART;
    }
    // 将所有信号添加到阻塞集中，阻塞信号在信号处理函数执行期间不会被传递
    sigfillset(&sa.sa_mask);
    // 设置信号处理动作，如果失败则输出错误信息
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时不断触发SIGALRM信号
void Utils::timer_handler(){
    m_timer_lst.tick();
    // m_TIMESLOT间隔过后触发alarm
    alarm(m_TIMESLOT);
}

// 向客户端发送错误信息
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}



class Utils;
void cb_func(client_data *user_data){
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}