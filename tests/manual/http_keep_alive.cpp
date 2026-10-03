#include "reactor/server.hpp"

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
        sleep(3);
    }
    cli_sock.Close();
    return 0;

}