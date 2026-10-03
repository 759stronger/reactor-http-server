#include "reactor/server.hpp"

// 回显
class EchoServer
{
private:
    TcpServer _server;

private:
    void OnMessage(const PtrConnection &conn, Buffer *buf)
    {
        conn->Send(buf->ReadPosition(), buf->ReadableSize());
         buf->MoveReadoffset(buf->ReadableSize());
        conn->Shutdown();
    }

    void OnConnected(const PtrConnection &conn)
    {
        DBG_LOG("new connection : %p", conn.get());
    }

    void OnClosed(const PtrConnection &conn)
    {
        DBG_LOG("close connection : %p", conn.get());
    }

public:
    EchoServer(int port) : _server(port)
    {
        _server.SetThreadCount(2);
        _server.EnableInactiveRelease(10);

        _server.SetClosedCallback(std::bind(&EchoServer::OnClosed, this, std::placeholders::_1));
        _server.SetConnectedCallback(std::bind(&EchoServer::OnConnected, this, std::placeholders::_1));
        _server.SetMessageCallback(std::bind(&EchoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void Start()
    {
        _server.Start();
    }
};