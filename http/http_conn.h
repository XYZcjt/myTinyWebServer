#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <string>
#include <string.h>
#include <netinet/in.h>
#include <map>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/mman.h>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"

using namespace std;

class http_conn
{
public:
    static const int FILENAME_LEN=200;  // 文件名字长度
    static const int READ_BUFFER_SIZE=2048;  // 读缓冲区长度
    static const int WRITE_BUFFER_SIZE=2048; // 写缓冲区长度
    // 报文请求的方法，只用到了GET和POST
    enum METHOD{
        GET=0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态机的状态
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE=0,  // 解析请求行
        CHECK_STATE_HEADER,         // 解析请求头
        CHECK_STATE_CONTENT         // 解析消息体，POST请求才使用
    };
    // 从状态机的状态
    enum LINE_STATUS{
        LINE_OK=0,                  // 完整读取一行
        LINE_BAD,                   // 报文语法错误
        LINE_OPEN                   // 读取行不完整
    };
    // 状态码，报文解析结果
    enum HTTP_CODE{
        NO_REQUEST,                 // 请求不完整，需要继续请求报文
        GET_REQUEST,                // 获得了完整的HTTP请求
        BAD_REQUEST,                // HTTP请求报文有语法错误
        NO_RESOURCE,                 // 
        FORBIDDEN_REQUEST,          // 
        FILE_REQUEST,               // 
        INTERNAL_ERROR,             // 服务器内部错误
        CLOSED_CONNECTION           // 
    };
    
public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有init，占位符？？？
    void init(int sockfd,const sockaddr_in &addr,char*,int,int,string user,string passwd,string sqlname);
    // 关闭http连接
    void close_conn(bool real_close=true);
    // 
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文
    bool write();
    // 
    sockaddr_in *get_address(){
        return &m_address;
    }
    // 初始化数据库中的数据，获得用户名和密码
    void initmysql_result(connection_pool *connPool);
    // 
    int timer_flag;
    // 
    int improv;

private:
    void init();
    // 从m_read_buf读取报文，并且处理
    HTTP_CODE process_read();
    // 向m_write_buf写入响应报文
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char* text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char* text);
    // 主状态机解析报文中的请求体数据
    HTTP_CODE parse_content(char* text);

    // get_line用于将指针向后偏移，指向未处理的字符
    char *get_line();
    // 从状态机读取一行，分析是请求报文的哪一行
    LINE_STATUS parse_line();

    void unmap();

    // 生成响应报文
    HTTP_CODE do_request();
    // 根据响应报文的格式，生成对应的部分，以下函数由do_request调用
    bool add_response(const char *format,...);
    bool add_content(const char *content);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL* mysql;
    // 读取报文为0，响应报文为1
    int m_state;

private:
    int m_sockfd;
    sockaddr_in m_address;

    // 存储读取的请求报文的数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 在缓冲区m_read_buf中数据最后一个字节的下一个位置
    long m_read_idx;
    // 缓冲区m_read_buf读取的位置
    long m_checked_idx;
    // 缓冲区m_read_buf已经解析的字符个数
    long m_start_line;

    // 存储响应报文的数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // ？？？
    long m_write_idx;

    // 主状态机的状态
    CHECK_STATE m_check_state;
    // 请求的方法
    METHOD m_method;

    // 存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;

    // 读取服务器上文件的地址
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    // 是否启用POST
    int cgi;
    // 存储请求头数据
    char *m_string;
    // 剩余发送的字节数
    int bytes_to_send;
    // 已发送的字节数
    int bytes_have_send;
    //网站根目录，文件夹内存放请求的资源和跳转的html文件
    char *doc_root;

    // ？？？
    map<string,string> m_users;
    // 为0，LT方式读取数据；为1，ET方式读取数据
    int m_TRIGMode;
    int m_close_log;

    // ？？？
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};



#endif