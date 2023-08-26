#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

using namespace std;

// 信号量
class sem{
public:
    sem(){
        // 第二个参数为0表示线程同步，为1表示进程同步
        if((sem_init(&m_sem,0,0))!=0){
            throw std::exception();
        }
    }
    sem(int num){
        if((sem_init(&m_sem,0,num))!=0){
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }

    bool wait(){
        return sem_wait(&m_sem)==0;
    }
    bool post(){
        return sem_post(&m_sem)==0;
    }

private:
    sem_t m_sem;
};

// 互斥锁
class locker
{
private:
    /* data */
    pthread_mutex_t m_mutex;
public:
    locker(){
        // 初始化成功返回0，否则返回非0
        if(pthread_mutex_init(&m_mutex, NULL)!=0){
            throw std::exception();
        }
    }
    ~locker(){
        // 成功返回0，否则返回非0
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }
    pthread_mutex_t* get(){
        return &m_mutex;
    }
};

// 条件变量
class cond{
public:
    cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex){
        return pthread_cond_wait(&m_cond,m_mutex)==0;
    }

    /*
    struct timespec {
        time_t tv_sec;  // 秒数
        long   tv_nsec; // 纳秒数
    };
    */
    bool timewait(pthread_mutex_t *m_mutex,struct timespec t){
        return pthread_cond_timedwait(&m_cond,m_mutex,&t)==0;
    }
    bool signal(){
        return pthread_cond_signal(&m_cond)==0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond)==0;
    }

private:
    pthread_cond_t m_cond;
};


#endif