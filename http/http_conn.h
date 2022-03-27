#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


/**
 * 把socket封装到一个类中
*/
class http_conn
{
public:
    // 文件名长度
    static const int FILENAME_LEN = 200;
    // 读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 报文的请求方法, 只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // 报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST, // 请求不完整，需要继续读取请求报文数据
        GET_REQUEST, // 获得了完整的HTTP请求
        BAD_REQUEST, // HTTP请求报文有语法错误
        NO_RESOURCE, 
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR, // 服务器内部错误
        CLOSED_CONNECTION
    };
    // 从状态机的状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭http连接
    void close_conn(bool real_close = true);
    void process();
    // 读取浏览器发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    
    // 同步线程初始化数据库读表
    void initmysql_result(connection_pool *connPool);
    
    // CGI使用线程池初始化数据库表
    // void initresultFile(connection_pool* connPool);
    
    int timer_flag;
    int improv;


private:
    void init();

    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE]; // 存储读取的请求报文数据
    int m_read_idx; // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx; // m_read_buff读取的位置m_checked_idx
    int m_start_line; // m_read_buff中已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx; // 指示buffer中的长度，同样表示的是下一个位置
    CHECK_STATE m_check_state;
    METHOD m_method;


    /*解析请求报文中对应的6个变量*/
    char m_real_file[FILENAME_LEN]; // 读取文件的名称
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;


    char *m_file_address; // 读取服务器上文件地址
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send; // 剩余发送字节数
    int bytes_have_send; // 已经发送的字节数
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
