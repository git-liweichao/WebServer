#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象, 最多有MAX_FD个
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root"; // 存放资源的文件夹
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD]; // 连接资源组
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
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

void WebServer::trig_mode()
{

    //LT + LT 
    if (0 == m_TRIGMode)
    {
        // 0表示的是LT模式 
        // 1表示的是ET模式
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

void WebServer::log_write()
{
    if (0 == m_close_log) // m_close_log标志位表示是否关闭日志文件:  0不关闭，1表示关闭
    {
        // 初始化日志, 有两种模式
        // 1表示的是异步, 0表示的是同步
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            // 同步模式的阻塞队列大小为0
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    
    // mysql的默认端口号为3306，这里写死了
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表, 这里读取了用户名+密码, 存储在哈希表中
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);


    /**
     * strcut linger用于空间连接的关闭
     * l_onoff = 0;l_linger忽略，close()立即返回，底层会将未发送完的数据发送完成后再释放资源
     * l_onoff != 0;l_linger=0，close()立即返回，但不会发送未发送完成的数据，而是通过一个REST包强制的关闭socket描述符，即强制退出
     * l_onoff != 0;l_linger > 0, close()不会立即返回，内核会延迟一段时间，这个时间就由l_linger的值来决定，如果超时时间到达之前，
     * 发送完未发送的数据（包括FIN包）并得到另一端的确认，close()会返回正确，close()会返回正确，socket描述符优雅性退出。否则，close()会
     * 直接返回错误值，未发送数据丢失，socket描述符被设置为非阻塞型，则close()会直接返回值
    */
    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); // 向epoll中注册m_listenfd套接字
    http_conn::m_epollfd = m_epollfd;

    // 这部分是fifo，创建管道套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]); // 设置管道写端非阻塞
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); // 向epoll中注册管道事件

    // 对一个对端已经关闭的socket调用两次write，第二次将会产生SIGPIPE信号，该信号默认结束进程
    // 这里使用SIG_IGN忽略信号SIGPIPE，防止结束进程，该信号的交付对线程没有影响
    // SIGKILL强制结束，SIGTERM释放资源后结束，SIGINT通过键盘ctrl+c结束
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false); // 定时器信号，传递给主循环的信号值
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT); // 单位是S

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd; // 定时器中使用到的
}


void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // http相关的初始化
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据，users_timer是专门用于存储该数据的数组
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func; // 设置定时器的回调函数
    time_t cur = time(NULL); // 获取当前时间
    timer->expire = cur + 3 * TIMESLOT; // 设置3倍的超时时间
    users_timer[connfd].timer = timer; // 将当前这个定时器赋给存储客户端数据的client_data
    utils.m_timer_lst.add_timer(timer); // 将该定时器添加到链表中，这里是实例化的一个工具类
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);  // 初始化定时器和客户端资源数据
    }

    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server) // 传入的参数是引用
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0); // 返回值都是1，只有14和15两个ASCII码对应的字符
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
                case SIGALRM: // 定时时间到达
                {
                    timeout = true;
                    break;
                }
                case SIGTERM: // 终止信号到达
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);  // 发生错误的情况需要删除定时器
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd); // 读取发生错误的话直接删除相应的定时器
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1); // 这里往工作队列中添加一个写任务，包括相应的状态标识0和1还有相应的sock描述符

        while (true) // 这里会一直循环直到工作线程完成写操作才会满足标志位条件
        {
            if (1 == users[sockfd].improv) // 等待线程池拿到并且完全写操作后user[sockfd].improv才会等于1
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd); // 发生错误的情况需要删除定时器
                    users[sockfd].timer_flag = 0; // 重新把相应的标志位置零
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false; // 超时默认为false
    bool stop_server = false;

    while (!stop_server)
    {
        // -1的原因是调试的过程中会发出相应的中断信号，导致其异常退出
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 为0的情况表示没有接收到
        // number需要>=1，才能进入循环中
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }

            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) // 处理异常事件
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }

            // 处理信号
            // 这里是特定的操作，定时器信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }

            // 处理客户连接上接收到的数据
            // 写入操作
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }

            // 写出操作
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }

        // 到达时间
        if (timeout)
        {
            // 定时器处理，对已有链表上的进行调整，同时重新设置定时信号可以触发
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}