#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    // 生成HTTP响应内容
    void MakeResponse(Buffer& buff);  //生成HTTP响应内容
    void UnmapFile();  //取消内存映射
    char* File();  // 获取内存映射的文件指针
    size_t FileLen() const;   // 获取内存映射的文件长度
    void ErrorContent(Buffer& buff, std::string message);  // 生成错误响应内容
    int Code() const { return code_; }  // 获取响应状态码

private:
    void AddStateLine_(Buffer &buff);  // 添加状态行
    void AddHeader_(Buffer &buff);  // 添加响应头
    void AddContent_(Buffer &buff);  // 添加响应内容

    void ErrorHtml_();  // 添加错误响应内容
    std::string GetFileType_();  // 获取文件类型

    int code_;  // 响应状态码  
    bool isKeepAlive_;  // 是否保持连接

    std::string path_;  // 请求的资源路径
    std::string srcDir_;  // 资源的物理路径
    
    char* mmFile_;   // 内存映射的文件指针  使用这个指针来对内容进行访问
    struct stat mmFileStat_;  // 内存映射的文件状态

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  // 文件后缀名对应的类型
    static const std::unordered_map<int, std::string> CODE_STATUS;  // 状态码对应的描述
    static const std::unordered_map<int, std::string> CODE_PATH; // 状态码对应的错误页面路径
};


#endif 