#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


// 线程池模板类的实现
template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    // 特别注意如果处理线程函数为类成员函数的时，需要将其设置为静态成员函数
    // 因为会传入this指针
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库连接池
    int m_actor_model;          //模型切换
};



template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        // this指针这里传入的是该线程池对象的地址
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) 
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) // 对线程设置自动分离回收状态
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads; // 析构函数逐一释放堆空间
}



// 当epoll监听到相应的事件时，将相应的socket放入到请求队列中
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 请求队列是主线程和工作线程共享的，所以要加锁
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) // 判断是否超过能加入请求队列中的最大数量
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state; // 设置requets的状态，这里表示的是http连接的状态
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 信号量表示可以处理的请求队列中的数量，生产者
    return true;
}


template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}


template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run(); // 一个线程池中的所有线程都是使用同一个run()
    return pool;
}


/**
 * 线程池运行的线程的函数
*/
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait(); // 消费者，如果没有请求队列的话都在这里进行阻塞
        m_queuelocker.lock(); // 请求队列中有对象的话，竞争获取互斥锁
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model) // 1是reactor模式，相应的读写、连接、客户逻辑都是由工作线程来完成，会插入到请求队列中
        {
            // 读为0，写为1
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1; // 读错的情况要置为1
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1; // 写成功的情况下
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1; // 没有写成功
                }
            }
        }
        else // 0是proactor模式，只负责客户端的逻辑
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool); 
            request->process();
        }
    }
}
#endif
