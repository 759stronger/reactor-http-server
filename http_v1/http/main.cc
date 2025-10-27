#include "http.hpp"

#define WWWROOT "./wwwroot/"

std::string RequestStr(const HttpRequest &req)
{
    std::stringstream ss;
    ss<<req._method << " "<<req._path <<" " << req._version <<"\r\n";
     for(auto &it : req._params )
    {
            ss<<it.first << ": " << it.second <<"r\n";
    }
    for(auto &it : req._headers )//放入头部字段
    {
            ss<<it.first << ": " << it.second <<"r\n";
    }
   
    ss<<"\r\n";
    ss<<req._body;
    return ss.str();
}
void Hello(const HttpRequest &req , HttpResponse  *rsp)
{
    rsp->SetContent(RequestStr(req) , "text/plain");
 
}

void Login(const HttpRequest &req , HttpResponse  *rsp)
{
    rsp->SetContent(RequestStr(req) , "text/plain");
}
void Putfile(const HttpRequest &req , HttpResponse  *rsp)
{
    std::string pathname = WWWROOT +req._path;
    Util::WriteFile(pathname ,req._body);
}
void Deletefile(const HttpRequest &req , HttpResponse  *rsp)
{
    rsp->SetContent(RequestStr(req) , "text/plain");
}
int main()
{
    HttpServer server(8085);
    server.SetThreadCount(3);
    server.SetBaseDir(WWWROOT); //设置静态资源根目录  告诉服务器 有静态资源请求去哪里找资源文件
    server.Get("/hello", Hello);
    server.Post("/login", Login);
    server.Put("/123.txt", Putfile);
    server.Delete("/123.txt", Deletefile);
    server.Listen();
    return 0;
}