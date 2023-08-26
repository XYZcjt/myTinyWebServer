#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

using namespace std;

template<typename T>
class threadpool
{
private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列的最大任务数
    
    pthread_t *m_threads;        // 工作线程
    list<T*>m_workqueue;          // 任务队列
    locker m_queuelocker;        // 使用队列时的互斥锁
    sem m_queuestate;            // 是否有任务要处理
    connection_pool *m_connPool; // 数据库连接
    int m_actor_model;           // 模型切换？？？


private:
    // 工作线程的运行函数，从任务队列取出任务执行
    static void *worker(void* arg);
    void run();

public:
    // 线程初始化
    threadpool(int actor_model, connection_pool *connPool, int thread_number=8, int max_requests=10000);
    ~threadpool();
    bool append(T *request,int state);
    bool append_p(T *request);
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests):m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool){
    if(thread_number<=0 || max_requests<=0){
        printf("threadpool初始化 error!\n");
        throw std::exception();
    }
    // 创建工作线程
    m_threads=new pthread_t[m_thread_number];   // 动态申请内存，记得清除
    if(!m_threads){
        throw std::exception();
    }
    for(int i=0; i<thread_number; i++){
        if(pthread_create(&m_threads[i], NULL, worker, this)!=0){
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

template<typename T>
bool threadpool<T>::append(T* request,int state){
    m_queuelocker.lock();
    // 任务队列满，添加失败
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;           // ？？？？？
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 唤醒消费者
    m_queuestate.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request){
    m_queuelocker.lock();
    // 任务队列满，添加失败
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 唤醒消费者
    m_queuestate.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool=(threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(true){
        m_queuestate.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        // 取出任务
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        if (1 == m_actor_model){
            // m_state(0) 表示 epollin 事件
            if (0 == request->m_state){
                if (request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            // m_state(1) 表示 epollout 事件
            else{
                if (request->write()){
                    request->improv = 1;
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else{
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif