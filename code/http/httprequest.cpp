#include "httprequest.h"
using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };
    //  当用户访问这些路径时，服务器会自动将其映射到对应的 HTML 文件

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };
    // 这些路径对应的标签，用于处理用户注册和登录请求
void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = "";  // 初始化成员变量
    state_ = REQUEST_LINE;   // 设置初始解析状态为 REQUEST_LINE
    header_.clear();  // 清空头部信息
    post_.clear();   // 清空 POST 数据
}

bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}   // 该函数通过检查请求头中的 Connection 字段和 HTTP 协议版本，判断是否启用持久连接（Keep-Alive）。

bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";  // 定义请求头的结束标志
    if(buff.ReadableBytes() <= 0) {
        return false;
    }  // 如果缓冲区没有可读数据，则返回 false，表示解析失败。
    while(buff.ReadableBytes() && state_ != FINISH) {  // 有可读数据且 能继续解析
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);  
        //[first1, last1)：主序列（待搜索的序列）  [first2, last2)：目标子序列（要查找的内容）
        //返回的指针指向第一个检索到的\r
        std::string line(buff.Peek(), lineEnd); // buff.Peek()-lineEnd-1 作为字符串line的值
        switch(state_)
        {
        case REQUEST_LINE:  
            if(!ParseRequestLine_(line)) {   // 解析请求行
                return false;
            }
            ParsePath_();  // 将路径映射为实际的 HTML 文件路径
            break;    
        case HEADERS:
            ParseHeader_(line);   //解析请求头
            if(buff.ReadableBytes() <= 2) {  // 如果缓冲区没有足够的数据来读取请求体，则返回 false。
                state_ = FINISH;
            }
            break;
        case BODY:  // 解析请求体
            ParseBody_(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()) { break; }  //没有更多数据可读，跳出循环。
        buff.RetrieveUntil(lineEnd + 2); //还可以读取的话跳过\r\n继续读取 改变read_pos位置
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

void HttpRequest::ParsePath_() {
    if(path_ == "/") {
        path_ = "/index.html";   //默认首页
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";
                break;
            }  // 如果请求的路径在默认 HTML 集合中，则将其后缀添加为 .html
        }
    }
}

bool HttpRequest::ParseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");  // 正则表达式匹配请求行格式
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {   
        method_ = subMatch[1];  // httpp请求方法
        path_ = subMatch[2];   // httpp请求路径
        version_ = subMatch[3];  // httpp版本
        state_ = HEADERS;  // 设置解析状态为 HEADERS，准备解析请求头
        return true;
    }
    LOG_ERROR("RequestLine Error");  // 请求行格式错误
    return false;
}

void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");  // 正则表达式匹配请求头格式
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];  // 将请求头的键值对存储到 header_ 中
    }
    else {
        state_ = BODY;  // 请求头解析完毕
    }
}

void HttpRequest::ParseBody_(const string& line) {
    body_ = line;  // 将请求体存储到 body_ 中
    ParsePost_();   /// 解析请求体
    state_ = FINISH;  // 设置解析状态为 FINISH，表示请求解析完成
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());  // 打印请求体信息
}

int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

//处理post请求体
void HttpRequest::ParsePost_() {
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        // 使用"application/x-www-form-urlencoded" 来进行数据交付
        ParseFromUrlencoded_();  
        if(DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);  // 如果 tag 为 1，则表示登录请求，否则为注册请求
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";  // 登录成功
                } 
                else {
                    path_ = "/error.html";  // 登录失败
                }
            }
        }
    }   
}

void HttpRequest::ParseFromUrlencoded_() {
    if(body_.size() == 0) { return; }  // 如果请求体为空，则直接返回

    string key, value;  // 键值对
    int num = 0;  // 键值对个数
    int n = body_.size();  // 请求体长度
    int i = 0, j = 0;  // i 用于遍历请求体， j 用于记录键的起始位置

// 键值对 + 分隔符   username=johndoe&password=123  
// 所有可能干扰解析的字符（如 &、=）、
// 非 ASCII 字符（如中文、 emoji）或空格，都会被转换为「% + 十六进制 ASCII 码」的形式，
// 这一过程称为 URL 编码（也叫「百分号编码」）。

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
        case '=':
            key = body_.substr(j, i - j);  // j开始  有i-j个字符
            j = i + 1;
            break;
        case '+':
            body_[i] = ' ';  //+ 在URL编码中表示空格
            break;
        case '%':
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);  //两位16进制数转换为10进制数
            body_[i + 2] = num % 10 + '0';  
            body_[i + 1] = num / 10 + '0';   
            i += 2;  // 跳过已转换的两位十六进制数
            //将 %20 → "32"（字符串）后，整个 body_ 字符串中就只包含普通的数字字符，
            break;
        case '&':  
            value = body_.substr(j, i - j);  // 从 j 到 i 的子字符串作为值
            j = i + 1;  
            post_[key] = value;  // 将键值对存储到 post_ 中
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j < i) {  //count等于0就是不存在
        value = body_.substr(j, i - j);
        post_[key] = value;  
        //可能存在最后一个键值对没有 '&' 结尾
    }
}


//传入post_["username"], post_["password"]对应的值
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") { return false; }  //输入为空
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;  
    SqlConnRAII(&sql,  SqlConnPool::Instance());  //从连接池中获取一个数据库连接。
    assert(sql);  //确保连接成功，否则程序终止。
    
    bool flag = false;  //表示是否成功（登录或注册）
    unsigned int j = 0;  //字段数量
    char order[256] = { 0 };  //SQL语句
    MYSQL_FIELD *fields = nullptr;  //字段信息
    MYSQL_RES *res = nullptr;  //结果集
    
    if(!isLogin) { flag = true; }   //注册行为，默认用户名未被使用
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    // 生成 SQL 查询语句，查询指定用户名的用户信息
    LOG_DEBUG("%s", order);  //打印 SQL 语句

    if(mysql_query(sql, order)) {     //执行 SQL 语句
        mysql_free_result(res);  // 释放结果集
        return false; 
    } // 如果查询失败，释放结果集并返回 false。

    res = mysql_store_result(sql);  // 获取结果集
    j = mysql_num_fields(res);   // 获取结果集中的字段数量
    fields = mysql_fetch_fields(res);  // 获取字段信息

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);  // 打印结果集中的数据
        string password(row[1]);                       // 获取查询到的密码
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }  // 密码匹配
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } 
        else { 
            flag = false;    // 用户名被使用
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);  // 释放结果集

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {  //
        LOG_DEBUG("regirster!");
        bzero(order, 256);  // 清空
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());  
        // 生成 SQL 插入语句，将用户名和密码插入到 user 表中
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) {   
            // 执行 SQL 插入语句  失败执行下面代码
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);  // 释放数据库连接
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;  //返回post_中key对应的value
    }
    return "";
}