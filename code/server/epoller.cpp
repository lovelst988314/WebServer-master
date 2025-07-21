#include "epoller.h"

Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){
    assert(epollFd_ >= 0 && events_.size() > 0);
    // 确保 epollFd_ 有效且事件列表不为空 
}

Epoller::~Epoller() {
    close(epollFd_);  // 关闭 epoll 文件描述符
}

bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);  // 添加描述符。 返回零表示成功
}

bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev); // 修改描述符。 
}

bool Epoller::DelFd(int fd) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);  // 删除文件描述符
}

int Epoller::Wait(int timeoutMs) {
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;  // 获取就绪事件的文件描述符
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;  // 获取就绪事件的类型
}


//  int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
//  epfd：epoll_create() 返回的 epoll 实例文件描述符。
//  op：操作类型，可选值：
//  EPOLL_CTL_ADD：注册新的文件描述符到 epfd。
//  EPOLL_CTL_MOD：修改已注册文件描述符的监听事件。
//  EPOLL_CTL_DEL：从 epfd 中删除文件描述符，此时 event 参数可传 NULL。
//  fd：要操作的目标文件描述符（如套接字、管道等）。
//  event：指向 struct epoll_event 的指针，指定监听的事件类型和关联数据。