#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class util_timer; // 前向声明，因为client_data中需要用到

struct client_data
{
    sockaddr_in address; // 客户端地址
    int sockfd; // 客户端socket描述符
    util_timer *timer; // 定时器，所以需要前向声明
};

// client中存储的定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; // 超时时间
    
    void (* cb_func)(client_data *); // 回调函数
    client_data *user_data; // 定时器对应的连接资源
    util_timer *prev; // 双向链表中的前向和后向
    util_timer *next;
};

// 定时器容器类
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer); // 添加定时器
    void adjust_timer(util_timer *timer); // 调整定时器
    void del_timer(util_timer *timer); // 删除定时器
    void tick(); // 按照规定的事件对定时器容器中进行删除

private:
    // 被公有的add_timer和adjust_timer调用，调整链表的内部节点
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head; // 头尾两个节点
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
