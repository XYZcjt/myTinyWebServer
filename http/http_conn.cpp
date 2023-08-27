#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

// sockaddr_in* http_conn::get_address(){
//     return &m_address;
// }


//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


locker m_lock;
// 存储用户
map<string,string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
    printf("服务器启动时，用户密码数据初始化！！！\n");
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        // LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        printf("SELECT error\n");
    }

    // 从表中检索完整的结果集
    // 从执行查询后的结果集中获取数据
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        printf("userName = %s, passWord = %s\n",row[0], row[1]);
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd){
    // 获取原来的文件描述符状态标志
    // F_GETFL用于获取文件状态标志，F_SETFL用于设置文件状态标志
    int old_option=fcntl(fd, F_GETFL);

    // 设置文件描述符为非阻塞模式
    int new_option=old_option | O_NONBLOCK;

    // 将新的状态标志应用到文件描述符，设置非阻塞模式
    fcntl(fd, F_SETFL, new_option);

    // 返回设置之前的文件描述符状态标志，在需要恢复阻塞模式时使用
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd,int fd,bool one_shot,int TRIGMode){
    epoll_event event;
    // 将文件描述符设置为事件的数据
    event.data.fd = fd;
    // TRIGMode为1表示使用ET模式，为0表示使用LT模式
    if(TRIGMode == 1){
        // EPOLLRDHUP事件，套接字的远程端关闭连接时触发
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(one_shot){
        // EPOLLONESHOT表示一次性触发模式
        event.events |= EPOLLONESHOT; 
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核事件表删除描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1){
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else{
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 用户连接量
int http_conn::m_user_count = 0;
// epoll描述符
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个，用户减一
void http_conn::close_conn(bool real_close){
    if(real_close && (m_sockfd != -1)){
        printf("close %d\n",m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接
void http_conn::init(int sockfd,const sockaddr_in &addr,char* root,int TRIGMode,
                    int close_log,string user,string passwd,string sqlname){
    m_sockfd=sockfd;
    m_address=addr;

    // 将sockfd套接字描述符加载到m_epoll内核事件表中，以便监听事件发生
    // m_TRIGMode表示epoll的不同触发模式(ET,LT)
    // 第三个参数表示，是否设置一次性触发
    addfd(m_epollfd,sockfd,true,m_TRIGMode);
    // 用户连接+1
    m_user_count++;

    // 网站根目录，文件夹内存放请求的资源和跳转的html文件
    doc_root=root;
    // ET模式或LT模式
    m_TRIGMode=TRIGMode;
    // 日志开关
    m_close_log=close_log;

    // 获得数据库相关数据
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    // 初始化内部数据
    init();
}

void http_conn::init(){
    mysql = NULL;
    bytes_have_send = 0;
    bytes_to_send = 0;
    // 默认为分析请求行状态
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，分析一行的内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
// m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
// m_checked_idx指向从状态机当前正在分析的字节
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_idx < m_read_idx; m_checked_idx++){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            // 下一个字符达到了buffer结尾，则接收不完整，需要继续接收
            if((m_checked_idx+1) == m_read_idx){
                return LINE_OPEN;
            }
            // 下一个字符是\n，将\r\n改为\0\0
            else if(m_read_buf[m_checked_idx+1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果都不符合，则返回语法错误
            return LINE_BAD;
        }
        // 如果当前字符是\n，也有可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if(temp == '\n'){
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r'){
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 并没有找到\r\n，需要继续接收
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    // 读取长度超过读缓冲区时，读取失败
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    // 存储接受到的字节数
    int bytes_read = 0;

    // LT读取数据
    if(m_TRIGMode == 0){
        // 参数1.表示要从哪个套接字接收数据
        // 参数2.指向缓冲区的指针
        // 参数3.希望接收的最大字节数
        // 参数4.标志位通常可以为 0，表示默认的接收方式(MSG_WAITALL)
        // MSG_WAITALL在接收到足够的数据之前，recv 调用会阻塞等待。
        // recv的阻塞方式还由套接字描述符的设置决定的
        bytes_read = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
        m_read_idx += bytes_read;
        if(bytes_read <= 0){
            return false;
        }
        return true;
    }
    // ET模式
    else{
        while(true){
            bytes_read = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
            if(bytes_read == -1){
                // EAGAIN 和 EWOULDBLOCK是类似的，用于表示资源暂时不可用
                // 通常在非阻塞模式下使用
                // 表示读缓冲区满了，需要再次重新读取
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    break;
                }
                return false;
            }
            // 读取内容为空
            else if(bytes_read==0){
                return false;
            }
            // 修改m_read_idx的位置指向
            m_read_idx += bytes_read;
        }
        return true;
    }
}

/*
GET /562f25980001b1b106000338.jpg HTTP/1.1
请求头
空行
请求数据为空
*/

/*
POST / HTTP1.1
Host:www.wrox.com
User-Agent:Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022)
Content-Type:application/x-www-form-urlencoded
Content-Length:40
Connection: Keep-Alive
空行
name=Professional%20Ajax&publisher=Wiley
*/



// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    printf("parse_request_line中的text: %s \n", text);
    // strpbrk 函数用于在一个字符串中查找 指定字符集合 中的任何字符的第一个匹配项
    // 返回值是一个指针
    m_url = strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }

    *m_url++ = '\0';

    printf("请求方法：%s\n", text);

    char* method = text;
    // strcasecmp 是一个用于比较两个字符串（不区分大小写）的函数
    // 两个字符串相等），返回 0。
    // 第一个字符串小于第二个字符串，返回一个负整数。
    // 第一个字符串大于第二个字符串，返回一个正整数。
    if(strcasecmp(method, "GET") == 0){
        m_method=GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;
    }
    else{
        return BAD_REQUEST;
    }

    // strspn 用于计算一个字符串中连续包含在指定字符集合中的字符数量
    // 如果没有则返回0，表示m_url后面没有多余的空格或者\t，是资源
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    printf("协议版本：%s\n", m_version);

    // 判断协议版本
    if(strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    
    // 资源前面带着前缀，把指针移动到前缀后面，方便读取资源路径
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        // strchr 函数用于在一个字符串中搜索指定字符的第一次出现，并返回该字符之后的部分字符串
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0){
        m_url += 8;
        m_url = strchr(m_url,'/');
    }

    // 直接是单独的/或/后面带访问资源
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    // m_url只携带有一个'/'
    if(strlen(m_url) == 1){
        // strcat用于将一个字符串追加到另一个字符串的末尾
        strcat(m_url, "judge.html");
    }
    printf("欢迎界面的url：%s\n", m_url);
    // 改变主状态机的状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    printf("parse_headers中的text: %s \n", text);
    // 判断是空行还是请求头
    if(text[0] == '\0'){
        printf("parse_headers中的解析空行\n");
        if(m_content_length != 0){
            printf("消息体不为空，为POST请求！！！\n");
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // 解析头部连接字段
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){
            // 长连接设置为true
            m_linger = true;
            printf("长连接！！！\n");
        }
    }
    // 解析头部内容长度字段
    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        // atol用于将字符串转换为长整型
        m_content_length = atol(text);
    }
    // 解析头部HOST字段
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        // LOG_INFO
        // 出现上面以外的关键词
        // printf("header出错: %s\n",text);
        printf("未知的头部关键字！！\n");
    }
    // 返回NO_REQUEST，继续往下判读空行或者POST请求的消息体 
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= (m_content_length+m_checked_idx)){
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        printf("parse_content中的m_string: %s\n", m_string);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
// 此时从状态机已提前将一行的末尾字符\r\n变为\0\0，text可以直接取出完整的行进行解析
char* http_conn::get_line(){
    return m_read_buf + m_start_line;
}

// 处理读取的报文
http_conn::HTTP_CODE http_conn::process_read(){
    // 初始化从状态机状态，HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 多了第一个判读条件是为了判断POST请求的消息体
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)){
        // 在调用get_line()之前，在while中已经调用了parse_line()函数
        text = get_line();

        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        printf("process_read中：%s\n\n",text);

        // 主状态机的三种状态转移
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                // 解析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                // 解析请求头
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                // 完整解析GET请求，跳转到报文响应函数
                else if(ret == GET_REQUEST){
                    printf("完整解析GET请求！！！\n");
                    return do_request();
                }
                printf("解析消息头！！！\n");
                break;
            }
            case CHECK_STATE_CONTENT:{
                printf("解析消息体！！！\n");
                // 解析消息体
                ret = parse_content(text);

                // 成功解析完消息体，跳转到报文响应函数
                if(ret == GET_REQUEST){
                    return do_request();
                }

                // 解析完消息体相当于解析完了整个报文，避免继续循环
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    // 报文解析不完整，继续请求
    return NO_REQUEST;
}

// 生成响应报文
http_conn::HTTP_CODE http_conn::do_request(){
    // m_real存储文件的路径，先把根路径复制进去，然后再存文件
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    printf("m_url的值是:%s\n", m_url);
    printf("doc_root + m_url的值是:%s%s\n", doc_root, m_url);
    // strrchr用于在一个字符串中查找指定字符的最后一个匹配项，并返回指向该位置的指针
    const char *p = strrchr(m_url, '/');

    printf("要处理的页面是：%s\n", p);
    printf("cgi = %d\n", cgi);
    printf("*(p+1) = %c\n", *(p+1));
    // 处理cgi
    // /2 POST请求，进行登录校验
    // /3 POST请求，进行注册校验
    if(cgi == 1 && (*(p+1)=='2' || *(p+1)=='3')){
        // 根据标志判断时登录检测还是注册检测
        printf("\ndo_request内部检测！！!\n");
        char flag = m_url[1];

        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url+2);
        strncpy(m_real_file+len, m_url_real, FILENAME_LEN-len-1);
        free(m_url_real);

        printf("m_real_file路径：%s\n", m_real_file);

        // 提取用户名和密码
        char name[100];
        char password[100];
        int i;
        // user=123&password=123
        // i=5从上面123中的1开始遍历
        for(i=5 ; m_string[i] != '&'; i++){
            name[i-5] = m_string[i];
        }
        name[i-5] = '\0';

        printf("用户名提取成功：%s \n", name);

        // 搞忘初始化了
        int j = 0;
        for(i = i+10; m_string[i] != '\0'; i++, j++){
            password[j] = m_string[i];
        }
        password[j] = '\0';

        printf("密码提取成功：%s \n", password);

        // 注册
        if(*(p+1)=='3'){
            printf("注册校验！！\n");
            // 注册先检验数据库中是否有重复的
            // 没有重复，增加数据
            char *sql_insert=(char*)malloc(sizeof(char)*200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if(users.find(name) == users.end()){
                printf("注册，数据库中有该用户：%s\n", name);
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();


                if(!res){
                    strcpy(m_url, "/log.html");
                }
                else{
                    strcpy(m_url, "/registerError.html");
                }
            }
            else{
                strcpy(m_url, "/registerError.html");
            }
        }

        // 登录
        else if(*(p+1) == '2'){
            if(users.find(name) != users.end() && users[name] == password){
                printf("登录，数据库中有该用户：%s\n", name);
                strcpy(m_url, "/welcome.html");
            }
            else{
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if(*(p+1) == '0'){
        printf("注册页面！！！\n");
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/register.html");
        // 将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        free(m_url_real);
        printf("处理注册页面结束！！！\n\n");
    }
    else if(*(p+1) == '1'){
        printf("登录页面！！！\n");
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/log.html");
        // 将网站目录和/log.html进行拼接，更新到m_real_file中
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        free(m_url_real);
        printf("登录页面处理结束！！！\n");
    }
    else if(*(p+1) == '5'){
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p+1) == '6'){
        char *m_url_real=(char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // else if(*(p+1) == '7'){
    //     char *m_url_real = (char*)malloc(sizeof(char)*200);
    //     strcpy(m_url_real, "/fans.html");
    //     strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
    //     free(m_url_real);
    // }
    else{
        // 如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        printf("欢迎页面！！\n");
        strncpy(m_real_file+len, m_url, FILENAME_LEN-len-1);
    }
        

    /*
    //获取文件属性，存储在statbuf中
    int stat(const char *pathname, struct stat *statbuf);
    
    struct stat 
    {
       mode_t    st_mode;        \\ 文件类型和权限 
       off_t     st_size;        \\ 文件大小，字节数
    };
    */

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }

    // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 使用 open 函数打开一个文件，m_real_file 是文件的路径，O_RDONLY 表示以只读模式打开文件。
    // open 函数会返回一个文件描述符，如果成功打开文件，则返回一个非负整数的文件描述符，否则返回 -1
    int fd = open(m_real_file, O_RDONLY);
    // mmap用于将一个文件或其他对象映射到内存，提高文件的访问速度。
    m_file_address=(char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 文件已经通过 mmap 映射到了内存，文件描述符不再需要
    close(fd);
    // 表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

// 接触内存映射
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}


bool http_conn::write(){
    int temp=0;

    // 若要发送的数据长度为0,表示响应报文为空
    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        // 重新初始化
        init();
        return true;
    }

    while(true){
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if(temp < 0){
            // 判断缓冲区是否满了
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            // 出错，解除映射
            unmap();
            return false;
        }

        // 已发送报文长度
        bytes_have_send += temp;
        // 剩余发送的长度
        bytes_to_send -= temp;

        // 第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send-m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        // 继续发送第一个iovec头部信息的数据
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 报文发送完
        if(bytes_to_send <= 0){
            // 解除内存映射
            unmap();
            // 修改读事件，继续读
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            // 长链接就初始化，重新开始读
            if(m_linger){
                init();
                return true;
            }
            // 短连接则直接断开
            else{
                return false;
            }
        }
    }
}

// 响应报文
bool http_conn::add_response(const char* format,...){
    // 如果写入内容超出m_write_buf大小则报错
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    // 定义可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化，并且传入参数
    va_start(arg_list, format);
    // vsnprintf可以将一个可变数量的参数根据格式化字符串的规则写入指定的字符数组中
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
    
    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if(len >= (WRITE_BUFFER_SIZE-1-m_write_idx)){
        va_end(arg_list);
        return false;
    }
    // 更新m_write_idx位置
    m_write_idx += len;
    // 清空可变参数列表
    va_end(arg_list);
    printf("request中:%s \n", m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len){
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content_Length:%d\r\n",content_len);
}

bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content){
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret){
    printf("运行到响应报文处理内部！！！\n");
    printf("HTTP_CODE为: %d", ret);
    switch (ret){
        // 内部错误
        case INTERNAL_ERROR:{
            printf("响应报文，内部错误！！！\n");
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)){
                return false;
            }
            break;
        }
        // 语法有错
        case BAD_REQUEST:{
            printf("响应报文，语法错误！！！\n");
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)){
                return false;
            }
            break;
        }
        // 资源没有访问权限
        case FORBIDDEN_REQUEST:{
            printf("响应报文，没有权限！！！\n");
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)){
                return false;
            }
            break;
        }
        // 文件存在
        case FILE_REQUEST:{
            printf("响应报文，文件存在！！！\n");
            add_status_line(200, ok_200_title);
            // 资源存在
            if (m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                // 指向响应报文缓冲区，长度为m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                // 指向mmap返回的文件指针，长度是文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            // 资源不存在
            else{
                // 返回空白文件
                printf("响应报文，资源不存在！！！\n");
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)){
                    return false;
                }
            }
        }
        default:{
            printf("运行到default了!!!\n");
            return false;
        }
            
    }

    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process(){
    // 报文解析
    HTTP_CODE read_ret = process_read();
    // 请求不完整，继续请求
    if (read_ret == NO_REQUEST){
        // 注册并监听事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    printf("process_read处理完了！！！！\n");
    // 报文响应
    bool write_ret = process_write(read_ret);
    if (!write_ret){
        printf("响应报文错误！！！\n");
        close_conn();
    }
    printf("process_write处理完了！！！！\n");
    // 注册并监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}