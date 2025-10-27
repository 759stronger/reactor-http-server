#include <iostream>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>


//简单的tcp服务器的搭建
int main()
{
    int sockfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); //创建套接字
    if(sockfd < 0 )
   {
      perror("socket error");
      return -1;
   }

   struct sockaddr_in addr;        //为套接字绑定一个地址信息
   addr.sin_family = AF_INET;
   addr.sin_port  = htons(8085);
   addr.sin_addr.s_addr = inet_addr("0.0.0.0");

   socklen_t len = sizeof(struct sockaddr_in);
   int ret = bind(sockfd , (struct sockaddr*)&addr, len  );
   if(ret <0 )
   {
    perror("bind error");
    return -1;
   }

   ret = listen(sockfd ,5);   //开始监听
   if(ret <0)
   {
    perror("listen error");
    return -1;
   }

   while(1)  
   {
       int newfd = accept(sockfd , NULL, 0); //循环中获取请求 接收新连接
       if(newfd <0)
       {
         perror("accept error ");
          return -1;
       }

       char buf[1024]={0};
       int ret = recv(newfd , buf ,1024 ,0 );
       if(ret <0)
       {
        perror("recv error");
        close (newfd);
        continue;
       }
       else if(ret ==0)
       {
        perror("peer shutdown");
        close(newfd);
        continue;
       }
       std::string body = "<html><body><h1>hello world</h1></body></html>";
       std::string rsp = "HTTP/1.1 200 OK \r\n";
       rsp+= "Content-Length:"+std::to_string(body.size())+"\r\n";
       rsp+= "Content-Type: text/html \r\n";
       rsp+= "\r\n";
       rsp+=body;

       ret = send(newfd , rsp.c_str(), rsp.size(),0); //组织数据后直接发送给客户端
       if(ret <0)
       {
        perror("send error");
        close(newfd);
        continue;
       }
       close(newfd);

   }
   close (sockfd);
   return 0;

}