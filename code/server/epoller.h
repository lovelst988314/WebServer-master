#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>

class Epoller {
public:
    explicit Epoller(int maxEvent = 1024);  //初始化 epoll 文件描述符并设置事件列表大小。

    ~Epoller();  

    bool AddFd(int fd, uint32_t events);  // 添加文件描述符到 epoll 实例，并设置监听的事件类型。

    bool ModFd(int fd, uint32_t events);  // 修改已添加的文件描述符的事件类型。

    bool DelFd(int fd);  // 从 epoll 实例中删除文件描述符。

    int Wait(int timeoutMs = -1);  // 等待事件发生，返回就绪的事件数量。

    int GetEventFd(size_t i) const;  // 获取就绪事件的文件描述符。

    uint32_t GetEvents(size_t i) const;  // 获取就绪事件的类型。
        
private:
    int epollFd_;  // epoll 文件描述符

    std::vector<struct epoll_event> events_;      // 存储就绪事件的 vector
};

#endif 