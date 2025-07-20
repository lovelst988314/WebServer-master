/*
 * @Author       : mark
 * @Date         : 2020-06-27
 * @copyleft Apache 2.0
 */ 
#include "httpresponse.h"

using namespace std;

const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};  // 文件后缀名对应的类型

const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};  // 状态码对应的描述

const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};   // 状态码对应的错误页面路径

HttpResponse::HttpResponse() {
    code_ = -1;   // 响应状态码
    path_ = srcDir_ = "";  // 请求的资源路径和资源的物理路径
    isKeepAlive_ = false;  // 是否保持连接
    mmFile_ = nullptr;   // 内存映射的文件指针
    mmFileStat_ = { 0 };  // 内存映射的文件状态
};

HttpResponse::~HttpResponse() {
    UnmapFile();
}

void HttpResponse::Init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    if(mmFile_) { UnmapFile(); }  // 如果当前有文件被映射到内存中，先解除映射
    code_ = code;  // 响应状态码
    isKeepAlive_ = isKeepAlive;   // 是否保持连接
    path_ = path;  // 请求的资源路径
    srcDir_ = srcDir;  // 资源的物理路径
    mmFile_ = nullptr;   // 内存映射的文件指针
    mmFileStat_ = { 0 };  // 内存映射的文件状态
}

void HttpResponse::MakeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        // 如果文件不存在或者是目录，则返回404错误
        code_ = 404;
    }
    else if(!(mmFileStat_.st_mode & S_IROTH)) {
        // 如果文件不可读，则返回403错误
        code_ = 403;
    }
    else if(code_ == -1) { 
        // 如果前面的条件都不满足，且 code_ 仍为 -1
        code_ = 200; 
    }
    ErrorHtml_();
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

char* HttpResponse::File() {
    return mmFile_;
}  // 获取内存映射的文件指针

size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}  // 获取内存映射的文件长度

void HttpResponse::ErrorHtml_() {
    if(CODE_PATH.count(code_) == 1) {
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }  // 如果状态码对应的错误页面路径存在，则使用该路径
}

void HttpResponse::AddStateLine_(Buffer& buff) {
    string status;
    if(CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
        // 如果状态码对应的描述存在，则使用该描述
    }
    else {
        code_ = 400;  // 如果状态码不存在，则默认为400错误
        status = CODE_STATUS.find(400)->second;  // 设置状态描述为 "Bad Request"
    }
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}  //响应的状态行

void HttpResponse::AddHeader_(Buffer& buff) {
    buff.Append("Connection: ");
    if(isKeepAlive_) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    } else{
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}  //将完整的 HTTP 响应头写入缓冲区，供后续发送

void HttpResponse::AddContent_(Buffer& buff) {
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);  // 打开文件以仅可读  返回文件描述符
    if(srcFd < 0) { 
        ErrorContent(buff, "File NotFound!");
        return; 
    }

    /* 将文件映射到内存提高文件的访问速度 
        MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
    LOG_DEBUG("file path %s", (srcDir_ + path_).data());  //    打印当前处理文件的路径
    int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if(*mmRet == -1) {
        ErrorContent(buff, "File NotFound!");  // 如果映射失败，返回错误内容
        return; 
    }
    mmFile_ = (char*)mmRet;  // 将映射的文件指针赋值给 mmFile_
    close(srcFd);  // 关闭源文件描述符  
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
    // 将文件内容添加到缓冲区
}

void HttpResponse::UnmapFile() {
    if(mmFile_) {
        //  如果不为空，说明当前有文件被映射到内存中，需要解除映射。
        //  mmFile_：指向内存映射区域的起始地址。
        //  mmFileStat_.st_size：映射区域的大小（即文件大小）。
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

string HttpResponse::GetFileType_() {
    /* 判断文件类型 */
    string::size_type idx = path_.find_last_of('.');  //没有找到返回 string::npos
    if(idx == string::npos) {
        // 没有后缀
        return "text/plain";
    }
    string suffix = path_.substr(idx);  // 获取文件后缀名
    if(SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";  // 如果后缀名不在 SUFFIX_TYPE 中，则默认为 "text/plain"
}

void HttpResponse::ErrorContent(Buffer& buff, string message) 
{
    //message 是错误信息
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status  + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}
// 生成错误响应内容，将错误信息写入缓冲区