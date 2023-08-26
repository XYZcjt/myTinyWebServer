#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>

#include "../lock/locker.h"

using namespace std;

class connection_pool{
public:
    // 获取数据库连接
    MYSQL *GetConnection();
    // 释放连接
    bool ReleaseConnection(MYSQL *conn);
    // 获取连接
    int GetFreeConn();
    // 销毁所以连接
    void DestroyPool();

    // 单例模式
    static connection_pool *GetInstance();

    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 


private:
    connection_pool();
    ~connection_pool();

    // 最大连接数
    int m_MaxConn;
    // 当前已使用的连接数
    int m_CurConn;
    // 当前空闲的连接数
    int m_FreeConn;
    locker lock;
    // 连接池
    list<MYSQL*> connList;
    sem reserve;

public:
    string m_url;
    string m_Port;
    string m_User;
    string m_PassWord;
    string m_DatabaseName;
    int m_close_log;	   // 日志开关
};

class connectionRAII{
public:
    connectionRAII(MYSQL **con,connection_pool* connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif