// 业务处理超时 查看服务器的处理情况
// 服务器达到性能瓶颈 一次业务处理中花费太长时间  超过了服务器设置的非活跃超时时间
// 1.在一次业务处理中耗费太长时间 导致其他的连接也被连累超时 其他的连接有可能被拖累超时释放
//   如果接下来的描述符都是通信连接描述符 如果都就绪了 就不影响 因为接下来会进行处理兵刷新活跃度
//   如果接下来的定时器事件描述符 定时器触发超时 执行定时任务  就会将后续描述符释放掉 一旦对应连接被释放 处理事件的时候就会导致程序崩溃内存访问错误
//       因此本次事件处理中 不能直接对连接进行释放 应该将释放操作压入任务池中  等到事件处理完了 执行任务池中的任务的时候 再去释放
//
// 2.

#include "reactor/http.hpp"

/*
int main()
{
    signal(SIGCHLD ,SIG_IGN);
    for (int i = 0; i < 10; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            DBG_LOG("fork error");
            return -1;
        }
        else if (pid == 0)
        {
            Socket cli_sock;
            cli_sock.CreateClient("127.0.0.1", 8085);
            std::string req = "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n";
            while (1)
            {
                assert(cli_sock.Send(req.c_str(), req.size()) != -1);
                char buf[1024] = {0};
                assert(cli_sock.Recv(buf, 1023));
                DBG_LOG("[%s]", buf);
            }
            cli_sock.Close();
            exit(0);
        }
    }
    while(1) sleep(1);

    return 0;
}*/

// 一次性给服务器发送多条数据 看服务器的处理结果
// 每一条都得到正常处理

/*
int main()
{
    Socket cli_sock;
    cli_sock.CreateClient( "127.0.0.1" ,8085);
    std::string req = "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n";
    req += "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n";
    req += "GET /hello HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n";
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

}*/

// 大文件传输测试
int main()
{
    Socket cli_sock;
    cli_sock.CreateClient( "127.0.0.1" ,8085);
    std::string req = "PUT /123.txt HTTP/1.1\r\nConnection: keep-alive\r\n";
    std::string body;
    Util::ReadFile("./tests/fixtures/hello.txt", &body);
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    assert(cli_sock.Send(req.c_str(), req.size()) != -1);
    assert(cli_sock.Send(body.c_str(), body.size()) != -1);
    char buf[1024] = {0};
    assert(cli_sock.Recv(buf, 1023));
    DBG_LOG("[%s]", buf);
    sleep(3);
    cli_sock.Close();
    return 0;
}