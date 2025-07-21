#include "webserver.h"

using namespace std;

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);
    HttpConn::userCount = 0;   // 初始化用户数量为0
    HttpConn::srcDir = srcDir_;  // 设置资源目录
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);
    // 设置数据库地址为 localhost

    InitEventMode_(trigMode);  // 初始化事件模式
    if(!InitSocket_()) { isClose_ = true;}   // 调用 InitSocket_() 创建并初始化监听套接字。

    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);  // 关闭监听套接字
    isClose_ = true;  // 设置服务器关闭标志
    free(srcDir_);  // 释放资源目录指针
    SqlConnPool::Instance()->ClosePool();  // 关闭数据库连接池
}

void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;   // 客户端关闭连接或写端关闭时，epoll 会报告此事件。
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;  
    //每个连接的事件只会触发一次（需要重新 MOD）。
    //如果客户端关闭连接，会触发 EPOLLRDHUP 事件，服务器可以及时清理资源。

    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);  // 设置 HttpConn 是否使用 ET 模式
}

void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();  // 获取下一个超时事件的时间间隔
        }
        int eventCnt = epoller_->Wait(timeMS);  //eventCnt 表示本次触发的事件数量。
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);  // 获取就绪事件的文件描述符
            uint32_t events = epoller_->GetEvents(i);  // 获取就绪事件的类型
            if(fd == listenFd_) {  
                DealListen_();    //如果 fd == listenFd_，表示有新客户端连接。
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);    //调用 CloseConn_() 关闭连接。
            }
            else if(events & EPOLLIN) {   // 读事件（客户端发送数据）
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if(events & EPOLLOUT) {  // 写事件（服务器发送数据）
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);  // 发送错误信息给客户端
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);  //记录日志
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());  // 从 epoll 实例中删除文件描述符
    client->Close();  // 关闭连接
}

void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);  // 初始化 HttpConn 对象
    if(timeoutMS_ > 0) {
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));  
        // this->CloseConn_(&users_[fd]);
        // 调用当前这个实例对象的CloseConn_函数
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    //  EPOLLIN：表示可读事件（客户端发送数据）。
    //  connEvent_：包含 EPOLLONESHOT | EPOLLRDHUP，用于防止并发处理和检测客户端断开。

    SetFdNonblock(fd); 
    //  调用 SetFdNonblock 方法将文件描述符设置为 非阻塞模式。
    //  非阻塞模式下，read() 和 write() 不会阻塞等待，提升并发性能。

    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);  // 接受新连接
        if(fd <= 0) { return;}  // 如果 accept 失败，直接返回
        else if(HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }   // 如果当前连接数超过最大限制，发送错误信息并返回
        AddClient_(fd, addr);  // 添加新客户端连接
    } while(listenEvent_ & EPOLLET);  // 
}

void WebServer::DealRead_(HttpConn* client) {
    assert(client);  
    ExtentTime_(client);  
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));  // 将读事件的处理任务添加到线程池中
}

void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));  // 将写事件的处理任务添加到线程池中
}

void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }  // 如果设置了超时时间，延长连接的超时时间
}

void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;  
    int readErrno = 0;
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }  // 如果读取数据失败且不是 EAGAIN 错误，关闭连接
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {   //解析 HTTP 请求并生成响应。
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);  // 将监听事件改为 EPOLLOUT（可写事件）。
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);  /// 如果解析失败，则将监听事件改为 EPOLLIN（可读事件）。
    }
}

void WebServer::OnWrite_(HttpConn* client) {  // 处理可写事件。
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */    // 表示当前响应数据已全部写入 socket。
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */  //  非阻塞模式下 socket 缓冲区已满），表示可以稍后继续写入。
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT); 
            //  调用 epoller_->ModFd(fd, EPOLLOUT)，重新监听该连接的写事件。
            return;
        }
    }
    CloseConn_(client);  // 如果写入数据后的错误码不是EAGAIN 或 连接不再保持活跃，关闭连接
}

/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }  //   端口号大于 65535 或小于 1024 时，返回错误。

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;  // 开启 SO_LINGER 选项
        optLinger.l_linger = 1;  // 关闭 socket 时最多等待 1 秒发送未发送的数据
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);  // 创建监听套接字
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }  // 创建监听套接字失败

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));  // 设置 SO_LINGER 选项

    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    } // 设置 SO_LINGER 选项失败

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));  // 设置端口复用选项
    //  SO_REUSEADDR 允许服务器在 TIME_WAIT 状态下重新绑定端口
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6);  // 监听套接字，允许最多 6 个未处理的连接排队
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);  // 将监听套接字添加到 epoll 实例中，监听读事件
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }  // 如果失败，关闭 socket 并返回 false

    SetFdNonblock(listenFd_);  // 设置监听套接字为非阻塞模式
    LOG_INFO("Server port:%d", port_);  // 输出监听端口信息
    return true;
}

int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);  // 设置非阻塞
}


