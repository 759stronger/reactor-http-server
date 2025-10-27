//给服务器发送一个数据 告诉他发送1024字节 但是实际不足1024  看服务器的处理结果

//1.如果数据只发送一次 服务器得不到完整请求，就不会进行业务处理 客户端得不到响应 最终超时关闭
//2.连着给服务器发送多次小的请求 服务器会将后面的请求当作前面请求的正文处理 然后因为处理错误关闭连接

#include "../http_v1/server.hpp"

int main()
{
    Socket cli_sock;
    cli_sock.CreateClient( "127.0.0.1" ,8085);
    std::string req = "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 100\r\n\r\n bitejiuyeke";
    while(1)
    {
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        assert(cli_sock.Send(req.c_str() , req.size())!= -1);
        char buf[1024] ={0};
        assert(cli_sock.Recv(buf,1023));
        DBG_LOG("[%s]" ,buf);
        sleep(3);
    }
    cli_sock.Close();
    return 0;

}