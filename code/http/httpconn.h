#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);  // 初始化连接

    ssize_t read(int* saveErrno);  // 读取数据

    ssize_t write(int* saveErrno);  // 发送数据

    void Close();  // 关闭连接
 
    int GetFd() const;  // 获取文件描述符

    int GetPort() const;  // 获取端口号

    const char* GetIP() const;  // 获取 IP 地址
    
    sockaddr_in GetAddr() const;  // 获取地址信息
    
    bool process();  // 处理 HTTP 请求

    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }  // 获取待写入的字节数

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }  // 检查连接是否保持活跃

    static bool isET;    // 是否使用 ET 模式
    static const char* srcDir;  // 资源的物理路径
    static std::atomic<int> userCount;  // 连接的用户数量
    
private:
   
    int fd_;  //客户端连接的 socket 文件描述符。
    struct  sockaddr_in addr_;  // 客户端地址信息。

    bool isClose_;  // 是否关闭连接
    
    int iovCnt_;  // iov 数组的元素个数
    struct iovec iov_[2];  // iov 数组，用于分散写操作
    
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;  // HTTP 请求对象，用于解析和存储请求信息
    HttpResponse response_;  // HTTP 响应对象，用于生成响应信息
};


#endif //HTTP_CONN_H