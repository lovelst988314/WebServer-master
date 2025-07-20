#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir;  // 资源的物理路径
std::atomic<int> HttpConn::userCount;  // 连接的用户数量
bool HttpConn::isET;  // 是否使用 ET 模式

HttpConn::HttpConn() { 
    fd_ = -1;   // 初始化文件描述符为 -1
    addr_ = { 0 };   // 初始化地址信息为全零
    isClose_ = true;  // 连接关闭
};

HttpConn::~HttpConn() { 
    Close(); 
};

void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;  // 用户数量加一
    addr_ = addr;  // 保存客户端地址信息
    fd_ = fd;   // 保存文件描述符
    writeBuff_.RetrieveAll();  // 清空写缓冲区
    readBuff_.RetrieveAll();  // 清空读缓冲区
    isClose_ = false;  // 连接未关闭
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);  // 记录连接信息
}

void HttpConn::Close() {
    response_.UnmapFile();  // 释放映射文件
    if(isClose_ == false){
        isClose_ = true;   // 标记连接为关闭
        userCount--;  // 用户数量减一
        close(fd_);  // 关闭文件描述符
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);  // 记录断开连接信息
    }
}

int HttpConn::GetFd() const {
    return fd_;
};

struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}  

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}  // 获取 IP 地址

int HttpConn::GetPort() const {
    return addr_.sin_port;
}  // 获取端口号

ssize_t HttpConn::read(int* saveErrno) {
    //  参数 saveErrno 用于传出错误码（如 EAGAIN、EWOULDBLOCK）
    ssize_t len = -1;
    do {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0) {
            break;
        }
    } while (isET);
    // 如果启用了边缘触发模式（isET == true），则尽可能多地读取所有数据，防止漏事件
    return len;
}   // 客户端读取服务器数据

ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;  
    do {
        len = writev(fd_, iov_, iovCnt_);  //   通过套接字向缓存区来写  
        // 实际写到客服端的数据长度 
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; }   //表示数据已全部发送完毕，退出循环
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {   // 如果写入的长度大于 iov_[0] 的长度
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            } //清空iov_[0]
        }
        else {
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            writeBuff_.Retrieve(len);  // 从写缓冲区中移除已发送的数据
        }
    } while(isET || ToWriteBytes() > 10240);
    return len;
}

bool HttpConn::process() {
    request_.Init();  // 初始化请求对象
    if(readBuff_.ReadableBytes() <= 0) {
        return false;
    }  // 如果读缓冲区没有可读数据，返回 false
    else if(request_.parse(readBuff_)) {
        LOG_DEBUG("%s", request_.path().c_str());  // 解析请求路径
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);  // 初始化响应对象
    } else {
        response_.Init(srcDir, request_.path(), false, 400);  // 如果解析失败，初始化响应对象为 400 错误
    }

    response_.MakeResponse(writeBuff_);  // 生成响应内容
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;  

    /* 文件 */
    if(response_.FileLen() > 0  && response_.File()) {  // 如果文件长度大于 0 且文件存在
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        // 设置 iov_[1] 的基地址和长度
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;   
}
