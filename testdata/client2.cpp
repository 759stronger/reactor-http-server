//超时连接测试1  创建一个客户端 给服务器发送一次数据后不动  看服务器是否会正常超时关闭连接

#include "../http_v1/server.hpp"

int main()
{
    Socket cli_sock;
    cli_sock.CreateClient( "127.0.0.1" ,8085);
    std::string req = "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n";
    while(1)
    {
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        char buf[1024] ={0};
        assert(cli_sock.Recv(buf,1023));
        DBG_LOG("[%s]" ,buf);
        sleep(15);
    }
    cli_sock.Close();
    return 0;

}