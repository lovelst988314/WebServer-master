#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    // 表示解析状态： 
    // REQUEST_LINE：解析请求行（方法、路径、版本）。
    // HEADERS：解析请求头。
    // BODY：解析请求体。
    // FINISH：解析完成。

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };
    //表示 HTTP 请求的状态码：
    // NO_REQUEST：未完成解析。
    // GET_REQUEST：成功解析 GET 请求。
    // BAD_REQUEST：请求格式错误。
    // NO_RESOURSE：资源不存在。
    // FORBIDDENT_REQUEST：权限不足。
    // FILE_REQUEST：请求文件。
    // INTERNAL_ERROR：服务器内部错误。
    // CLOSED_CONNECTION：连接关闭。

    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();  // 初始化请求
    bool parse(Buffer& buff);  // 解析请求

    std::string path() const;  // 获取请求路径
    std::string& path();  // 获取请求路径
    std::string method() const;  // 获取请求方法
    std::string version() const;  //获取 HTTP 版本
    std::string GetPost(const std::string& key) const; // 获取 POST 请求参数
    std::string GetPost(const char* key) const; // 获取 POST 请求参数

    bool IsKeepAlive() const;  // 检查连接是否保持活跃

    /* 
    todo 
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    bool ParseRequestLine_(const std::string& line);  // 解析请求行
    void ParseHeader_(const std::string& line);  // 解析请求头
    void ParseBody_(const std::string& line);  // 解析请求体

    void ParsePath_();  // 解析路径
    void ParsePost_();  // 解析 POST 请求
    void ParseFromUrlencoded_();  // 解析 URL 编码的表单数据

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);  // 用户验证

    PARSE_STATE state_;  // 当前解析状态
    std::string method_, path_, version_, body_;  //存储 HTTP 请求的方法、路径、版本和请求体。
    std::unordered_map<std::string, std::string> header_;  // 存储请求头
    std::unordered_map<std::string, std::string> post_;  // 存储 POST 请求参数

    static const std::unordered_set<std::string> DEFAULT_HTML;  // 默认 HTML 文件集合
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;  // 默认 HTML 文件标签映射
    static int ConverHex(char ch);  // 将十六进制字符转换为整数
};


#endif //HTTP_REQUEST_H