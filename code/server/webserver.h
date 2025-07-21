#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../http/httpconn.h"

class WebServer {
public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);
    
    // 端口号 port
    // 事件触发模式 trigMode（如 EPOLLLT、EPOLLET）
    // 连接超时时间 timeoutMS
    // 是否开启 SO_LINGER 选项 OptLinger
    // 数据库连接信息（端口、用户名、密码、数据库名）
    // 连接池、线程池大小
    // 日志相关参数（是否启用、日志级别、队列大小）

    ~WebServer();
    void Start();  // 启动服务器

private:
    bool InitSocket_();   // 初始化监听套接字
    void InitEventMode_(int trigMode);  // 初始化事件触发模式
    void AddClient_(int fd, sockaddr_in addr);  // 添加新客户端连接
  
    void DealListen_();  // 处理监听事件
    void DealWrite_(HttpConn* client);  // 处理写事件
    void DealRead_(HttpConn* client);  // 处理读事件

    void SendError_(int fd, const char*info);  // 发送错误信息给客户端
    void ExtentTime_(HttpConn* client);  // 延长连接时间
    void CloseConn_(HttpConn* client);   // 关闭连接

    void OnRead_(HttpConn* client);  // 处理读事件
    void OnWrite_(HttpConn* client);  // 处理写事件
    void OnProcess(HttpConn* client);  // 处理业务

    static const int MAX_FD = 65536;  // 最大文件描述符数量

    static int SetFdNonblock(int fd);   // 设置文件描述符为非阻塞模式

    int port_;   // 监听端口
    bool openLinger_;   // 是否启用 SO_LINGER。
    int timeoutMS_;  /* 毫秒MS */
    bool isClose_;  // 是否关闭服务器
    int listenFd_;  // 监听套接字文件描述符
    char* srcDir_;   // 服务器资源目录
    
    uint32_t listenEvent_;   // 监听事件类型
    uint32_t connEvent_;   // 连接事件类型
   
    std::unique_ptr<HeapTimer> timer_;  // 定时器，用于管理连接超时
    std::unique_ptr<ThreadPool> threadpool_;  // 线程池，用于处理请求
    std::unique_ptr<Epoller> epoller_;  // epoll 实例，用于事件通知
    std::unordered_map<int, HttpConn> users_;   // 存储所有连接的用户
}; 
#endif 