#include "../http_v1/server.hpp"

EventLoop base_loop;
uint64_t _conn_id = 0;
LoopThreadPool *loop_pool;

void OnMessage(const PtrConnection &conn, Buffer *buf)
{
    DBG_LOG("%s", buf->ReadPosition());
    buf->MoveReadoffset(buf->ReadableSize());
    std::string str = "hello world";
    conn->Send(str.c_str(), str.size());
}

std::unordered_map<uint64_t, PtrConnection> _conns;
void ConnectionDestroy(const PtrConnection &conn)
{
    _conns.erase(conn->Id());
}
void OnConnected(const PtrConnection &conn)
{
    DBG_LOG("new connection : %p", conn.get());
}
void NewConnection(int fd)
{
    _conn_id++;
    PtrConnection conn(new Connection(loop_pool->NextLoop(), _conn_id, fd));
    conn->SetMessageCallback(std::bind(OnMessage, std::placeholders::_1, std::placeholders::_2));
    conn->SetClosedCallback(std::bind(ConnectionDestroy, std::placeholders::_1));
    conn->SetConnectedCallback(std::bind(OnConnected, std::placeholders::_1));

    conn->EnableInactiveRelease(10); // 启动非活跃超时销毁
    conn->Established();             // 就绪初始化
    _conns.insert(std::make_pair(_conn_id, conn));

    DBG_LOG("new ------------------ ");
}
int main()
{
    loop_pool = new LoopThreadPool(&base_loop);
    loop_pool->SetThreadCount(2);
    loop_pool->Create();
    Acceptor acceptor(&base_loop, 8500);
    // 为监听套接字 创建一个channel进行事件的管理 以及事件的处理
    // Channel channel(&loop, lst_sock.Fd());
    // 为channel设置回调函数
    acceptor.SetAcceptCallback(std::bind(NewConnection, std::placeholders::_1)); // 获取新连接 为新连接创建channel并且添加监控
                                                                                 // 启动可读事件监控
    acceptor.Listen();

    // 获取新连接
    /* int newfd = lst_sock.Accept();
     if (newfd < 0) // 失败了
     {
         continue;
     }
     Socket cli_sock(newfd); // 成功了就实例化一个出来
     char buf[1024] = {0};   // 接收数据
     int ret = cli_sock.Recv(buf, 1023);
     if (ret < 0)
     {
         cli_sock.Close();
         continue;
     }
     cli_sock.Send(buf, ret); // 回显
     cli_sock.Close();

     std::vector<Channel*> actives;
     poller.Poll(&actives);
     for(auto &a :actives)
     {
         a->HandleEvent();
     }*/

    base_loop.Start();

    return 0;
}
