#ifndef __M_SERVER_H__
#define __M_SERVER_H__
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <ctime>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <typeinfo>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL ERR
#define LOG(level, format, ...)                                                                                       \
    do                                                                                                                \
    {                                                                                                                 \
        if (level < LOG_LEVEL)                                                                                        \
            break;                                                                                                    \
        time_t t = time(NULL);                                                                                        \
        struct tm *ltm = localtime(&t);                                                                               \
        char tmp[32] = {0};                                                                                           \
        strftime(tmp, 31, "%H:%M:%S", ltm);                                                                           \
        fprintf(stdout, "[%p %s %s:%d]" format "\n", (void *)pthread_self(), tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)

#define MAX_LISTEN 8085

#define BUFFER_DEFAULT_SIZE 1024
class Buffer // 缓冲区类的实现
{
private:
    std::vector<char> _buffer; // 使用vector进行内存空间管理
    uint64_t _read_idx;        // 读偏移
    uint64_t _write_idx;       // 写偏移

public:
    Buffer() : _read_idx(0), _write_idx(0), _buffer(BUFFER_DEFAULT_SIZE) {}

    // 获取当前写入起始地址
    char *begin()
    {
        return &*_buffer.begin(); // 返回一个迭代器指向的第0个元素  再取地址
    }
    char *WritePosition() //_buffer的空间起始地址加上写偏移量
    {
        return begin() + _write_idx;
    }

    // 获取当前读取起始地址
    char *ReadPosition()
    {
        return begin() + _read_idx;
    }

    // 获取前沿空间大小  写偏移之后的空闲空间  总体空间大小减去写偏移
    uint64_t TailIdleSize()
    {
        return _buffer.size() - _write_idx;
    }

    // 获取后沿空间大小  读偏移之前的空闲空间
    uint64_t HeadIdleSize()
    {
        return _read_idx;
    }

    // 获取可读数据大小   写偏移减去读偏移
    uint64_t ReadableSize()
    {
        return _write_idx - _read_idx;
    }

    // 将读偏移向后移动
    void MoveReadoffset(uint64_t len)
    {
        if (len == 0)
            return;
        assert(len <= ReadableSize()); // 向后移动的大小必须小于等于可读大小
        _read_idx = _read_idx + len;
    }

    // 将写偏移向后移动
    void MoveWriteoffset(uint64_t len)
    {
        assert(len <= TailIdleSize());
        _write_idx += len;
    }

    // 确保可写空间足够 (整体空间)
    void Ensure_writespace_enough(uint64_t len)
    {
        if (TailIdleSize() >= len)
            return;                                 // 末尾的够直接返回
        if (len <= TailIdleSize() + HeadIdleSize()) // 总体够 把数据移动到起始位置
        {
            uint64_t rsz = ReadableSize();                            // 把当前数据大小先保存起来
            std::copy(ReadPosition(), ReadPosition() + rsz, begin()); // 把可读数据拷贝到起始位置
            _read_idx = 0;                                            // 将读偏移归0
            _write_idx = rsz;                                         // 写位置置为可读数据大小
        }
        else // 总体不够 扩容 不移动数据 直接给写偏移之后扩容足够空间
        {
            _buffer.resize(_write_idx + len);
        }
    }

    // 写入数据
    void Writedata(const void *data, uint64_t len)
    {
        // 1.保证有足够的空间  2.拷贝数据进去
        if (len == 0)
            return;
        Ensure_writespace_enough(len);
        const char *d = (const char *)data;
        std::copy(d, d + len, WritePosition());
    }
    void WriteAndPush(const void *data, uint64_t len)
    {
        Writedata(data, len);
        MoveWriteoffset(len);
    }
    void WriteString(const std::string &data)
    {
        return Writedata(data.c_str(), data.size());
    }
    void WriteStringAndPush(const std::string &data)
    {
        WriteString(data);
        MoveWriteoffset(data.size());
    }
    void WriteBuffer(Buffer &data)
    {
        return Writedata(data.ReadPosition(), data.ReadableSize());
    }
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteoffset(data.ReadableSize());
    }

    // 读取数据
    void Readdata(void *buf, uint64_t len)
    {
        // 要获取的数据大小 小于可读数据大小
        assert(len <= ReadableSize());
        std::copy(ReadPosition(), ReadPosition() + len, (char *)buf); // 这里要强转
    }
    void ReadAndPop(void *buf, uint64_t len)
    {
        Readdata(buf, len);
        MoveReadoffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string str;
        str.resize(len);
        Readdata(&str[0], len);
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string str = ReadAsString(len);
        MoveReadoffset(len);
        return str;
    }

    char *FindCRLF() // 寻找换行字符
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadableSize());
        return res;
    }

    std::string Getline() // 通常获取一行数据 这种情况针对是
    {
        char *pos = FindCRLF();
        if (pos == NULL)
        {
            return "";
        }
        return ReadAsString(pos - ReadPosition() + 1); //+1是把换行字符也取出来
    }
    std::string GetLineAndPop()
    {
        std::string str = Getline();
        MoveReadoffset(str.size());
        return str;
    }

    // 清空缓冲区
    void clear()
    {
        // 只需将偏移量归0即可
        _write_idx = 0;
        _read_idx = 0;
    }
};

// socket 模块
class Socket
{
private:
    int _sockfd;

public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }
    int Fd() { return _sockfd; }
    // 创建套接字
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sockfd < 0)
        {
            ERR_LOG("Create Socket Failed ");
            return false;
        }
        return true;
    }

    // 绑定地址信息
    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;                      // 组织一个地址结构
        addr.sin_family = AF_INET;                    // ipv4地址类型
        addr.sin_port = htons(port);                  // 端口转成网络字节序
        addr.sin_addr.s_addr = inet_addr(ip.c_str()); // ip地址
        socklen_t len = sizeof(struct sockaddr_in);   //

        int ret = bind(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("Bind Address Failed ");
            return false;
        }
        return true;
    }

    // 开始监听
    bool Listen(int backlog = MAX_LISTEN) // 限制同一时间的并发连接数
    {
        // int listen(int backlog)
        int ret = listen(_sockfd, backlog);
        if (ret < 0)
        {
            ERR_LOG("Socket Listen  Failed ");
            return false;
        }
        return true;
    }

    // 向服务器发起连接
    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;                      // 组织一个地址结构
        addr.sin_family = AF_INET;                    // ipv4地址类型
        addr.sin_port = htons(port);                  // 端口转成网络字节序
        addr.sin_addr.s_addr = inet_addr(ip.c_str()); // ip地址
        socklen_t len = sizeof(struct sockaddr_in);   //

        int ret = connect(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("Connect Server Failed ");
            return false;
        }
        return true;
    }

    // 获取新连接
    int Accept()
    {
        int newfd = accept(_sockfd, NULL, NULL);
        if (newfd < 0)
        {
            ERR_LOG("Socket Accept Failed ");
            return -1;
        }
        return newfd;
    }

    // 接受数据
    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if (ret <= 0)
        {
            if (errno == EAGAIN || errno == EINTR) // EAGAIN当前缓冲区没有数据 在非阻塞的情况下才有这个错误
            {                                      // EINTER 当前的socket的阻塞等待被信号打断了
                return 0;                          // 表示这次接收没有接收到数据
            }
            ERR_LOG("Socket Recv Failed ");
            return -1;
        }
        return ret; // 实际接收的数据长度
    }

    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT); // 表示当前接收为非阻塞
    }
    // 发送数据
    ssize_t Send(const void *buf, size_t len, int flag = 0) // 有可能buf中的数据没有发送完
    {
        ssize_t ret = send(_sockfd, buf, len, flag);
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("Socket Send Failed ");
            return -1;
        }
        return ret; // 实际发送的数据长度
    }
    ssize_t NonBlockSend(void *buf, size_t len)
    {
        if (len == 0)
            return 0;
        return Send(buf, len, MSG_DONTWAIT); // 表示当前发送为非阻塞
    }

    // 关闭套接字
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }

    // 创建一个服务端连接  不用ip地址 因为是绑定所有
    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 1.创建套接字
        if (Create() == false)
            return false;
        if (block_flag)
            NonBlock();
        // 2.绑定地址
        if (Bind(ip, port) == false)
            return false;
        // 3.开始监听
        if (Listen() == false)
            return false;
        // 4.设置非阻塞
        
        // 5.设置地址重用
        ReuseAddress();
        return true;
    }
    // 创建一个客户端连接
    bool CreateClient(const std::string &ip, uint16_t port)
    {
        // 创建套接字
        if (Create() == false)
            return false;
        // 直接连接服务器
        if (Connect(ip, port) == false)
            return false;
        return true;
    }
    // 设置套接字选项 开启地址端口重用
    void ReuseAddress()
    {
        // int setsockopt(int fd ,int leve, int optname , void * val , int vallen)
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int)); // 地址重用
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(int)); // 端口重用
    }

    // 设置套接字阻塞属性 设置为非阻塞
    void NonBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
};

// 对描述符的监控事件管理
class Poller;
class EventLoop;
class Channel
{
private:
    int _fd;
    EventLoop *_loop;
    uint32_t _events;  // 当前需要监控事件
    uint32_t _revents; // 当前连接触发的事件
    using EventCallback = std::function<void()>;
    EventCallback _read_callback;  // 可读事件被触发的回调函数
    EventCallback _write_callback; // 可写事件被触发的回调函数
    EventCallback _error_callback; // 错误事件
    EventCallback _close_callback; // 连接断开事件
    EventCallback _event_callback; // 任意事件

public:
    Channel(EventLoop *loop, int fd) : _fd(fd), _events(0), _revents(0), _loop(loop) {}
    int Fd() { return _fd; }
    void SetREvents(uint32_t events) { _revents = events; } // 设置实际就绪的事件
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }

    uint32_t Events() { return _events; } // 获取想要监控的事件
    // 当前是否监控了可读
    bool Readable() { return (_events & EPOLLIN); }
    // 当前是否监控了可写
    bool Writeable() { return (_events & EPOLLOUT); }
    // 启动读事件监控
    void EnableRead()
    {
        _events |= EPOLLIN;
        Update();
    } // 后边回添加到Eventloop的事件监控中
    // 启动写事件监控
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }
    // 关闭读事件监控
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Update();
    }
    // 关闭写事件监控
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }
    // 关闭所有事件监控
    void DisableAll()
    {
        _events = 0;
        Update();
    }
    // 移除监控  后边会调用eventloop接口来移除监控
    void Remove();
    void Update();

    // 事件处理 一旦连续触发了事件
    void HandleEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            
            if (_read_callback)
                _read_callback();
        }

        // 有可能会释放连接的操作事件 一次只处理一个
        if (_revents & EPOLLOUT)
        {
             // 不管任何事件都调用的回调函数
            if (_write_callback)
                _write_callback();
        }
        else if (_revents & EPOLLERR)
        {
            
            if (_error_callback)
                _error_callback(); // 一旦出错就会释放连接 连接都释放了就不能调用任意事件回调 但可以放在前面
        }
        else if (_revents & EPOLLHUP)
        {
            
            if (_close_callback)
                _close_callback();
        }
        if (_event_callback)
                _event_callback(); // 不管任何事件都调用的回调函数  放在事件处理完后调用 刷新活跃度
    }
};

#define MAX_EPOLLEVENTS 1024
class Poller
{
private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS];
    std::unordered_map<int, Channel *> _channels;

private:
    // 对epoll的直接操作
    void Update(Channel *channel, int op)
    {
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            ERR_LOG("EPOLLCTL failed ");
            abort(); // 退出程序
        }
        return;
    }
    // 判断一个channel是否已经添加了事件监控
    bool HasChannel(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
        {
            return false;
        }
        return true;
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            ERR_LOG("epoll create failed ");
            abort(); // 退出程序
        }
    }
    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (ret == false)
        {
            // 不存在就添加   要把channel在对象内管理起来
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD); // 添加事件的监控
        }
        return Update(channel, EPOLL_CTL_MOD); // 更新
    }

    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channels.find(channel->Fd()); // 管理的信息删除
        if (it != _channels.end())
        {
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL); // 移除
    }

    // 开始监控 返回活跃连接数组
    void Poll(std::vector<Channel *> *active)
    {
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("EPOLL WAIT ERROR: %s \n", strerror(errno));
            abort();
        }
        for (int i = 0; i < nfds; i++)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            it->second->SetREvents(_evs[i].events); // 设置实际就绪的事件
            active->push_back(it->second);
        }
        return;
    }
};

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;

class TimerTask
{
private:
    uint64_t _id;         // 定时器任务对象id
    uint32_t _timeout;    // 定时任务的超时时间
    bool _canceled;       // false 表示没有被取消  true表示被取消
    TaskFunc _task_cb;    // 定时器对象要执行的定时任务
    ReleaseFunc _release; // 用于删除TimerWheer中保存的定时器对象信息
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb) : _id(id), _timeout(delay), _task_cb(cb), _canceled(false)
    {
    }
    ~TimerTask()
    {
        if (_canceled == false)
            _task_cb(); // 定时任务就是这时候执行
        _release();
    }
    void Cancel()
    {
        _canceled = true;
    }
    void SetRelease(const ReleaseFunc &cb)
    {
        _release = cb;
    }
    uint32_t DelayTime()
    {
        return _timeout;
    }
};

class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int _tick;     // 秒针  走到哪里 释放哪里 相当于执行哪里的任务
    int _capacity; // 表盘最大数量 最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;

    EventLoop *_loop;
    int _timerfd; // 定时器描述符 可读事件回调就是读取计数器 执行定时任务
    std::unique_ptr<Channel> _timer_channel;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }
    static int CreateTimerfd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd < 0)
        {
            ERR_LOG("timer create failed");
            return -1;
        }
        struct itimerspec itime;
        itime.it_value.tv_sec = 1;
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1;
        itime.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, NULL);
        return timerfd;
    }
    int ReadTimefd()
    {
        uint64_t times;//有可能因为其他描述符处理花费时间比较长 然后再处理定时器描述符事件的时候 就可能已经超时了很多次了
        //read读取到的数据times 就是从上一次read之后超时的次数
        int ret = read(_timerfd, &times, 8);
        if (ret < 0)
        {
            ERR_LOG("read Timerfd failed ");
            abort();
        }
        return  times;
    }
    void RunTimerTask() // 这个函数应该每秒钟执行一次 相当于秒针向后走了一步
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear(); // 清空指定位置的数组 就会把数组中保存的所有管理定时器对象的shared_ptr释放掉
    }

    void Ontime() // 时间到了
    {
       int times =   ReadTimefd();
       for(int i =0 ;i<times ;i++) //根据实际超时的次数来执行超时任务
       {
            RunTimerTask();
       }
    }
    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb) // 添加定时任务
    {
        PtrTask pt(new TimerTask(id, delay, cb));                      // new一个定时器任务处理 实例化对象
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id)); // 当定时任务对象被销毁 要从轮子里移除信息
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }

    void TimerRefreshInLoop(uint64_t id) // 刷新/延迟定时任务
    {
        // 通过保存的定时器对象的weakptr构造一个sharedptr出来，添加到轮子中
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return; // 没找到定时任务
        }
        PtrTask pt = it->second.lock(); // lock获取weak_ptr管理的对象对应的shared_ptr
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }
    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return; // 没找到定时任务
        }
        PtrTask pt = it->second.lock();
        if (pt)
            pt->Cancel();
    }

public:
    TimerWheel(EventLoop *loop) : _capacity(60), _tick(0), _wheel(_capacity),
                                  _loop(loop), _timerfd(CreateTimerfd()), _timer_channel(new Channel(_loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::Ontime, this));
        _timer_channel->EnableRead(); // 启动读事件监控
    }

    // 定时器中有个_timers成员 定时器信息的操作有可能在多线程中进行 因此需要考虑线程安全问题
    // 如果不想加锁 那就把对定期的所有操作 都放到一个线程中进行
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);
    void TimerRefresh(uint64_t id);
    void TimerCancel(uint64_t id);

    bool HasTimer(uint64_t id) // 存在线程安全问题 只能在Eventloop里调用 不在被外界使用者调用
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return false; // 没找到定时任务
        }
        return true;
    }
};

class EventLoop
{
private:
    Poller _poller;
    int _event_fd;                           // eventfd来唤醒IO事件监控有可能导致的阻塞
    std::thread::id _thread_id;              // 线程id
    std::unique_ptr<Channel> _event_channel; // 管理eventfd的事件

    using Functor = std::function<void()>;
    std::vector<Functor> _tasks; // 任务池
    std::mutex _mutex;           // 任务池操作的线程安全

    TimerWheel _timer_wheel;

public:
    // 执行任务池中的所有任务
    void RunAllTask()
    {
        std::vector<Functor> functor;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor); // 把任务移到functor中
        }
        for (auto &f : functor)
        {
            f(); // 运行任务
        }
        return;
    }

    static int CreateEventfd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            ERR_LOG("Create eventfd failed !!");
            abort();
        }
        return efd;
    }
    void ReadEventfd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret < 0)
        {
            if (errno == EINTR || errno == EAGAIN) // 被信号打断 无数据可读
            {
                return;
            }
            ERR_LOG("Read eventfd failed !!");
            abort();
        }
        return;
    }
    void WeakUpEventfd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof(val));
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("Weakup eventfd failed !!");
            abort();
        }
        return;
    }

public:
    EventLoop() : _thread_id(std::this_thread::get_id()), _event_fd(CreateEventfd()),
                  _event_channel(new Channel(this, _event_fd)), _timer_wheel(this)
    {
        // 给eventfd添加可读事件回调函数 读取eventfd事件通知次数
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventfd, this));
        // 启动读事件监控
        _event_channel->EnableRead();
    }

    bool IsInLoop() // 判断当前线程是否是Eventloop对应的线程
    {
        return _thread_id == std::this_thread::get_id();
    }
    void AssertInLoop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }
    void RunInLoop(const Functor &cb) // 判断将要执行的任务是否处于当前线程中 如果是则执行 不是则压入队列中
    {
        if (IsInLoop())
        {
            return cb();
        }
        return QueueInLoop(cb); // 没在就压入任务池中
    }

    void QueueInLoop(const Functor &cb) // 将操作压入任务池
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        // 唤醒有可能因为没有事件就绪 而导致的epoll阻塞
        // 就是给eventfd写入一个数据  eventfd就会触发可读事件 就不会阻塞了
        WeakUpEventfd();
    }

    void UpdateEvent(Channel *channel) // 添加或修改描述符的监控事件
    {
        return _poller.UpdateEvent(channel);
    }
    void RemoveEvent(Channel *channel) // 移除描述符的监控
    {
        return _poller.RemoveEvent(channel);
    }

    void Start()
    {
        while (1)
        {
            // 1.事件监控
            std::vector<Channel *> actives; // 接收活跃连接
            _poller.Poll(&actives);
            // 2.就绪事件处理
            for (auto &channel : actives)
            {
                channel->HandleEvent();
            }
            // 3.执行任务
            RunAllTask();
        }
    }

    void AddTimer(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        return _timer_wheel.TimerAdd(id, delay, cb);
    }

    void TimerRefresh(uint64_t id) // 延迟定时任务
    {
        return _timer_wheel.TimerRefresh(id);
    }

    void TimerCancel(uint64_t id) // 延迟定时任务
    {
        return _timer_wheel.TimerCancel(id);
    }
    bool HasTimer(uint64_t id)
    {
        return _timer_wheel.HasTimer(id);
    }
};

class LoopThread
{
private:
    // 实现_loop获取同步关系   避免线程创建了 但是_Loop还没实例化之前去获取_loop
    std::mutex _mutex;             // 互斥锁
    std::condition_variable _cond; // 条件变量

    EventLoop *_loop;    // loop对象的指针变量  这个对象在线程内实例化
    std::thread _thread; // Eventloop对应的线程
private:
    // 实例化loop对象  并且运行loop模块的功能
    void ThreadEntry()
    {
        EventLoop loop;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;      // 赋值
            _cond.notify_all(); // 唤醒阻塞的线程
        }
        loop.Start();
    }

public:
    // 创建线程 设定线程入口函数
    LoopThread() : _loop(NULL), _thread(std::thread(&LoopThread::ThreadEntry, this)) {}
    // 返回当前线程关联的loop对象指针
    EventLoop *GetLoop()
    {
        EventLoop *loop = NULL;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock, [&]()
                       { return _loop != NULL; }); // loop为空就一直阻塞
            loop = _loop;
        }
        return loop;
    }
};

class LoopThreadPool
{
private:
    int _thread_count; // 线程数量
    int _next_loop_idx;
    EventLoop *_baseloop;
    std::vector<LoopThread *> _threads;
    std::vector<EventLoop *> _loops;

public:
    LoopThreadPool(EventLoop *baseloop) : _thread_count(0), _next_loop_idx(0), _baseloop(baseloop) {}
    void SetThreadCount(int count) { _thread_count = count; }
    void Create()
    {
        if (_thread_count > 0)
        {
            _threads.resize(_thread_count);
            _loops.resize(_thread_count);
            for (int i = 0; i < _thread_count; i++)
            {
                _threads[i] = new LoopThread();
                _loops[i] = _threads[i]->GetLoop();
            }
        }
        return;
    }
    EventLoop *NextLoop()
    {
        if (_thread_count == 0)
        {
            return _baseloop;
        }
        _next_loop_idx = (_next_loop_idx + 1) % _thread_count;
        return _loops[_next_loop_idx];
    }
};

// 通用容器 保存各种数据
class Any
{

private:
    class holder // 嵌套的父类  只需要几个虚函数就可以了
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info &type() = 0; // 获取保存的数据类型
        virtual holder *clone() = 0;              // 能够针对一个子类对象克隆出一个新的对象出来
    };

    template <class T>                // 模板类
    class placeholder : public holder // 子类 继承了父类
    {
    public:
        placeholder(const T &val) : _val(val) {}

        virtual const std::type_info &type() // 获取子类对象保存的数据类型
        {
            return typeid(T);
        }
        virtual holder *clone() // 针对当前子类对象克隆一个新的子类对象出来
        {
            return new placeholder(_val);
        }

    public: // 有一个成员 保存任意类型的数据  可以返回类型
        T _val;
    };

    holder *_content; // 父类指针

public:
    Any() : _content(nullptr) {} // 空构造
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) // 直接给数据构造通用容器
    {
        // 考虑为val new一个子类对象出来
    }
    Any(const Any &other) : _content(other._content ? other._content->clone() : nullptr) // 也可能给一个容器构造一个新的容器
    {
        // 首先判断通融容器内部有没有保存数据 有就克隆一份保存 但是不能保存你保存的指针
    }
    ~Any()
    {
        delete _content;
    }
    Any &swap(Any &other)
    {
        std::swap(_content, other._content); // 交换一下指针
        return *this;
    }

    template <class T>
    T *get() // 返回子类对象保存的数据的指针
    {
        assert(typeid(T) == _content->type());      // 想要获取的数据类型必须和保存的数据类型一致
        return &((placeholder<T> *)_content)->_val; // 先类型转换 再取地址
    }

    // 重载等号运算符
    template <class T>
    Any &operator=(const T &val)
    {
        // 为val构造一个临时的通用容器 然后与当前容器自身(this)进行指针交换 临时对象释放的时候 原先保存的数据也就被释放掉了
        Any(val).swap(*this);
        return *this;
    }

    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

// 连接关闭状态   连接成功,待处理状态   已连接状态(设置已完成 可以通信)     待关闭状态
typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
} ConnStatu;
class Connection;
using PtrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _conn_id;             // 连接的唯一id 便于连接的管理和查找  同时简化connid作为定时器id
    int _sockfd;                   // 连接关联的文件描述符
    ConnStatu _statu;              // 连接状态
    bool _enable_inactive_release; // 连接是否启动非活跃销毁的判断标志 默认为false
    Socket _socket;                // 套接字的操作管理
    Channel _channel;              // 连接的事件管理
    Buffer _in_buffer;             // 输入缓冲区    存放从socket中读取到的数据
    Buffer _out_buffer;            // 输出缓冲区  存放要发送给客户端的数据
    Any _context;                  // 请求的接收处理上下文
    EventLoop *_loop;              // 连接所关联的一个loop

    // 这几个回调函数 是让服务器模块，组件使用者来设置的
    // 即 这几个回调都是组件使用者使用的
    using ConnectedCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection &)>;
    using AnyCallback = std::function<void(const PtrConnection &)>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyCallback _event_callback;
    // 组件内的连接关闭回调 组件内设置 因为服务器组件内会把所有的连接管理起来 一旦某个连接要关闭 就应该从管理的地方移除掉自己的信息
    ClosedCallback _server_close_callback;

private:
    // 五个channel的事件回调函数
    void HandleRead() // 描述符可读事件回调用的函数 接收socket数据放到缓冲区中 然后调用_message_callback
    {
        // 接收socket数据 放入缓冲区
        char buf[65536];
        int ret = _socket.NonBlockRecv(buf, 65535);
        if (ret < 0)
        { // 出错了不能直接关闭连接 应该看一下还有没有数据待处理
            return ShutdownInLoop();
        } // 没读到数据 不是连接断开
        _in_buffer.WriteAndPush(buf, ret);
        // 调用messagecallback进行处理
        if (_in_buffer.ReadableSize() > 0)
        { // 从当前对象自身获取自身的shared_ptr管理对象
            return _message_callback(shared_from_this(), &_in_buffer);
        }
    }

    void HandleWrite() // 描述符可写事件回调用的函数
    {
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadableSize());
        if (ret < 0)
        {
            // 发送错误就该关闭连接
            if (_in_buffer.ReadableSize() > 0)
            {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            return Release(); // 实际关闭
        }
        _out_buffer.MoveReadoffset(ret); // 将读偏移向后移动
        if (_out_buffer.ReadableSize() == 0)
        {
            _channel.DisableWrite(); // 没有数据待发送 关闭写事件监控
            // 如果当前是连接带关闭状态 则有数据  发送完数据释放连接 没有数据则直接释放
            if (_statu == DISCONNECTING)
            {
                return Release();
            }
        }
        return;
    }

    void HandleClose() // 描述符触发挂断事件
    {
        if (_in_buffer.ReadableSize() > 0) // 一旦连接挂断 套接字就什么都做不了 有数据就处理 然后关闭
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        return Release(); // 实际关闭
    }

    void HandleError() // 描述符触发出错事件
    {
        return HandleClose();
    }
    void HandleEvent() // 描述符触发任意事件
    {
        // 刷新活跃度 延迟定时销毁任务
        if (_enable_inactive_release == true)
        {
            _loop->TimerRefresh(_conn_id);
        }
        // 调用组件使用者的任意事件回调
        if (_event_callback)
        {
            _event_callback(shared_from_this());
        }
    }

    void EstablishedInLoop() // 连接获取之后 所处的状态下要进行的各种设置 给channel设置事件回调 启动读监控
    {
        // 修改连接状态
        assert(_statu == CONNECTING); // 当前的状态必须一定是半连接状态
        _statu = CONNECTED;
        // 启动读监控
        _channel.EnableRead();
        if (_connected_callback)
            _connected_callback(shared_from_this());
        // 调用回调函数
    }
    void ReleaseInLoop() // 实际的释放(关闭连接)接口
    {
        // 修改连接状态
        _statu = DISCONNECTED;
        // 移除连接的事件监控
        _channel.Remove();
        // 关闭描述符
        _socket.Close();
        // 如果当前定时器队列中还有定时销毁任务 取消
        if (_loop->HasTimer(_conn_id))
            CancelInactiveReleaseInLoop();
        // 调用关闭回调函数 避免先移除服务器管理的连接信息导致connection释放，再去处理会出错 所以先移除用户的
        if (_closed_callback)
            _closed_callback(shared_from_this());
        if (_server_close_callback)
            _server_close_callback(shared_from_this());
    }

    void SendInLoop(Buffer &buf) // 把数据放入发送缓冲区 启动可写事件监控
    {
        if (_statu == DISCONNECTED)
            return;
        _out_buffer.WriteBufferAndPush(buf);
        if (_channel.Writeable() == false)
        {
            _channel.EnableWrite();
        }
    }
    void ShutdownInLoop()
    {
        _statu = DISCONNECTING;
        if (_in_buffer.ReadableSize() > 0)
        {
            if (_message_callback)
                _message_callback(shared_from_this(), &_in_buffer);
        }
        if (_out_buffer.ReadableSize() > 0)
        {
            if (_channel.Writeable() == false)
            {
                _channel.EnableWrite();
            }
        }
        if (_out_buffer.ReadableSize() == 0)
        {
            Release();
        }
    }

    void EnableInactiveReleaseInLoop(int sec)
    {
        // 判断标志 置为true
        _enable_inactive_release = true;
        // 添加定时销毁任务
        if (_loop->HasTimer(_conn_id))
        {
            return _loop->TimerRefresh(_conn_id);
        }
        _loop->AddTimer(_conn_id, sec, std::bind(&Connection::Release, this));
    }
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if (_loop->HasTimer(_conn_id))
            _loop->TimerCancel(_conn_id);
    }

    void UpgradeInLoop(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg,
                       const ClosedCallback &closed, const AnyCallback &event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg;
        _closed_callback = closed;
        _event_callback = event;
    }

public:
    Connection(EventLoop *loop, uint64_t conn_id, int sockfd) : _conn_id(conn_id), _sockfd(sockfd),
                                                                _enable_inactive_release(false), _loop(loop), _statu(CONNECTING), _socket(_sockfd), _channel(loop, _sockfd)
    {
        _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this));
        _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
        _channel.SetReadCallback(std::bind(&Connection::HandleRead, this));
        _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this));
        _channel.SetErrorCallback(std::bind(&Connection::HandleError, this));
    }
    ~Connection() { DBG_LOG("RELEASE CONNECTION: %p", this); }

    // 获取管理的文件描述符
    int Fd() { return _sockfd; }
    // 获取连接ID
    int Id() { return _conn_id; }
    // 是否处于CONNECTED状态
    bool Connected() { return _statu == CONNECTED; }
    // 设置上下文  连接建立完成时调用
    void SetContext(const Any &context)
    {
        _context = context;
    }
    // 获取上下文   返回指针
    Any *GetContext() { return &_context; }

    void SetConnectedCallback(const ConnectedCallback &cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback &cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback &cb) { _closed_callback = cb; }
    void SetAnyCallback(const AnyCallback &cb) { _event_callback = cb; }
    void SetSrvclosedCallback(const ClosedCallback &cb) { _server_close_callback = cb; }

    // 连接建立就绪后 进行channel回调设置 启动读监控 调用_connected_callback
    void Established() { _loop->RunInLoop(std::bind(&Connection::EstablishedInLoop, this)); }

    // 发送数据 存入缓冲区 启动写事件监控
    // 外界传入的data 可能是临时空间 我们只是把发送操作压入任务池 没有立即执行 因此可能存在执行时，data指向的空间以及被释放了
    void Send(const char *data, size_t len)
    {
        Buffer buf;
        buf.WriteAndPush(data, len);
        _loop->RunInLoop(std::bind(&Connection::SendInLoop, this, std::move(buf)));
    }

    // 提供给组件使用者的关闭接口  并不实际关闭 需要判断有没有数据待处理
    void Shutdown() { _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop, this)); }

    void Release()
    {
        _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop ,this ));
    }

    // 启动非活跃销毁 并定义多长时间无通信就是非活跃 添加定时任务
    void EnableInactiveRelease(int sec) { _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop, this, sec)); }

    // 取消非活跃销毁
    void CancelInactiveRelease() { _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop, this)); }

    // 切换协议  重置上下文以及阶段性处理函数  非线程安全的
    void Upgrade(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg,
                 const ClosedCallback &closed, const AnyCallback &event)
    {
        _loop->AssertInLoop(); // 这个接口必须在eventloop中立即执行 防备新的事件触发后 处理的时候切换任务还没有被执行 会导致数据使用原协议处理了
        _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop, this, context, conn, msg, closed, event));
    }
};

class Acceptor
{
private:
    Socket _socket;   // 用于创建监听套接字
    EventLoop *_loop; // 用于对监听套接字进行事件监控
    Channel _channel; // 用于对监听套接字进行事件管理

    using AcceptCallback = std::function<void(int)>;
    AcceptCallback _accept_callback;

private:
    // 监听套接字的读事件回调处理函数 获取新连接  调用_accept_callback函数进行新连接处理
    void HandleRead()
    {
        int newfd = _socket.Accept();
        if (newfd < 0)
        {
            return;
        }
        if (_accept_callback)
            _accept_callback(newfd);
    }

    int ACreateServer(int port)
    {
        bool ret = _socket.CreateServer(port);
        assert(ret == true);
        return _socket.Fd();
    }

public:
    Acceptor(EventLoop *loop, int port) : _socket(ACreateServer(port)), _loop(loop),
                                          _channel(loop, _socket.Fd())
    {
        _channel.SetReadCallback(std::bind(&Acceptor::HandleRead, this));
        //_channel.EnableRead();
    }
    void SetAcceptCallback(const AcceptCallback &cb)
    {
        _accept_callback = cb;
    }

    // 不能将启动读事件监控放到构造函数中 必须在设置回调函数后 再去启动
    // 否则有可能造成启动监控后 立即有事件 处理的时候 回调函数还没设置 新连接得不到处理 且资源泄露
    void Listen() { _channel.EnableRead(); }
};

class TcpServer
{
private:
    int _timeout;  //非活跃度连接的统计时间  多长时间无通信就是非活跃连接
    int _port;
    uint64_t next_id;     // 自动增长的连接id
    Acceptor _acceptor;   // 监听套接字的管理对象
    EventLoop _baseloop;  // 主线程的loop对象 负责监听事件的处理
    LoopThreadPool _pool; // 从属线程池
    bool _enable_inactive_release;

    std::unordered_map<uint64_t, PtrConnection> _conns; // 保存管理所有连接对应的shared_ptr;

    using ConnectedCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection &)>;
    using AnyCallback = std::function<void(const PtrConnection &)>;
    using Functor = std::function<void()>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyCallback _event_callback;

private:
    // 为新连接构造一个connection进行管理
    void NewConnection(int fd)
    {
        next_id++;
        PtrConnection conn(new Connection(_pool.NextLoop(), next_id, fd));
        conn->SetMessageCallback(_message_callback);
        conn->SetClosedCallback(_closed_callback);
        conn->SetConnectedCallback(_connected_callback);
        conn->SetAnyCallback(_event_callback);
        conn->SetSrvclosedCallback(std::bind(&TcpServer::RemoveConnection,this,std::placeholders::_1));
        if(_enable_inactive_release) conn->EnableInactiveRelease(_timeout); // 启动非活跃超时销毁
        conn->Established();             // 就绪初始化
        _conns.insert(std::make_pair(next_id, conn));
    }

    // 从管理connection的 _conns中移除连接信息
    void RemoveConnectionInLoop(const PtrConnection &conn)
    {
        int id = conn->Id();
        auto it = _conns.find(id);
        if(it != _conns.end())
        {
            _conns.erase(it);
        }
    }
    void RemoveConnection(const PtrConnection &conn) 
    {
        _baseloop.RunInLoop(std::bind(&TcpServer::RemoveConnectionInLoop , this , conn));
    }

    void RunAfterInLoop(const Functor &task, int delay)
    {
        next_id++;
        _baseloop.AddTimer(next_id, delay, task);
    }

public:
    TcpServer(int port) : _port(port), next_id(0), _enable_inactive_release(false),
                          _acceptor(&_baseloop, port), _pool(&_baseloop)
    {
        _acceptor.SetAcceptCallback(std::bind(&TcpServer::NewConnection,this , std::placeholders::_1));
        _acceptor.Listen(); // 将监听套接字挂到baseloop上开始监控事件
    }
    void SetThreadCount(int count) { return _pool.SetThreadCount(count); }

    void SetConnectedCallback(const ConnectedCallback &cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback &cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback &cb) { _closed_callback = cb; }
    void SetAnyCallback(const AnyCallback &cb) { _event_callback = cb; }

    void EnableInactiveRelease(int timeout)
    {
        _timeout = timeout;
        _enable_inactive_release = true;
    }
    void RunAfter(const Functor &task, int delay) // 添加一个定时任务
    {
        _baseloop.RunInLoop(std::bind(&TcpServer::RunAfterInLoop, this, task, delay));
    }

    void Start()
    {
        _pool.Create();// 创建从属线程

        return _baseloop.Start();
    }
};

void Channel::Remove() { return _loop->RemoveEvent(this); } // 实现和声明分离
void Channel::Update() { return _loop->UpdateEvent(this); }

void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}
void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}


class NetWork
{
    public:
        NetWork()
        {
            DBG_LOG("SIFPIPE INIT");
            signal(SIGPIPE,SIG_IGN);
        }
};
static NetWork nw;

#endif