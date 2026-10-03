#include "reactor/server.hpp"


void OnMessage(const PtrConnection &conn, Buffer *buf)
{
    DBG_LOG("%s", buf->ReadPosition());
    buf->MoveReadoffset(buf->ReadableSize());
    std::string str = "hello world";
    conn->Send(str.c_str(), str.size());
}

void OnConnected(const PtrConnection &conn)
{
    DBG_LOG("new connection : %p", conn.get());
}

void OnClosed(const PtrConnection &conn)
{
    DBG_LOG("close connection : %p", conn.get());
}

int main()
{
    TcpServer server(8500);
    server.SetThreadCount(2);
    server.EnableInactiveRelease(10);

    server.SetClosedCallback(OnClosed);
    server.SetConnectedCallback(OnConnected);
    server.SetMessageCallback(OnMessage);

    server.Start();
    return 0 ;
    
}