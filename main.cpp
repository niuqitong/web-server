#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>

#include "locker.h"
#include "thread_pool.h"
#include "http_connection.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000

// add signal capture, 
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa)); // or bzero()
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert( sigaction( sig, &sa, NULL ) != -1 );
    // sigaction(sig, &sa, NULL);
}

// add/remove fd to/from epoll
extern void addfd(int epoll_fd, int fd, bool one_shot);
extern void removefd(int epoll_fd, int fd);
extern void modifyfd(int epoll_fd, int fd, int event);

int main(int argc, char* argv[]) {

    if (argc <= 1) {
        printf("invalid commands/arguments\n");
        exit(-1);
    }

    // get port number
    // argv[0] is the name of the executable
    int port = atoi(argv[1]);

    // process SIGPIPE

    addsig(SIGPIPE, SIG_IGN);

    // initialize thread pool
    thread_pool<http_connection>* pool = NULL;
    try {
        pool = new thread_pool<http_connection>;
    } catch(...) {
        exit(-1);
    }

    // clients' requests information
    http_connection* users = new http_connection[MAX_FD];

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    /*  
        int socket(int domain, int type, int protocol);
        socket()创建一个通信端点, 返回指向该端点的文件描述符, 该文件描述符是当前进程可用的值最小的文件描述符
        socket()  creates  an  endpoint  for communication and returns a file descriptor that refers to that endpoint.
        The file descriptor returned by a successful call will be the lowest-numbered file  descriptor  not  currently
        open for the process
    */

    // 设置端口复用 
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    /*  
        int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
        When  manipulating  socket options, the level at which the option resides and the name of
        the option must be specified.  To manipulate options at the sockets API level,  level  is
        specified as SOL_SOCKET
        The arguments optval and optlen are used to access option values for  setsockopt() 
    */
    // bind
    struct sockaddr_in address; 
    address.sin_family = AF_INET; // 2 bytes
    address.sin_addr.s_addr = INADDR_ANY; // 4 bytes
    address.sin_port = htons(port); // 2 bytes
    // padding

    /*     
        struct sockaddr_in
        {
            __SOCKADDR_COMMON (sin_);
            in_port_t sin_port;			/* Port number.  
            struct in_addr sin_addr;		/* Internet address.  

            /* Pad to size of `struct sockaddr'.  
            unsigned char sin_zero[sizeof (struct sockaddr) -
                    __SOCKADDR_COMMON_SIZE -
                    sizeof (in_port_t) -
                    sizeof (struct in_addr)];
        };
    */
    int ret = 0;
    ret = bind(listen_fd, (struct sockaddr*)&address, sizeof(address));
    /* 
        
        int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
        socket被创建出之后, 存在于它的命名空间中(地址族), 但是没有分配给他的地址.
        bind()将addr内的地址分配给这个sockfd, addrlen表示addr指向的地址所占空间大小
        When  a  socket is created with socket(2), it exists in a name space (address family) but
        has no address assigned to it.  bind() assigns the  address  specified  by  addr  to  the
        socket  referred to by the file descriptor sockfd.  addrlen specifies the size, in bytes,
        of the address structure pointed to by addr.  Traditionally,  this  operation  is  called
        “assigning a name to a socket”.

     */
    // listen
    ret = listen(listen_fd, 5);
    /* 
        int listen(int sockfd, int backlog);
        listen()表示sockfd指向的socket是被动的, 这个socket使用 accept() 接受连接请求
        backlog表示sockfd所支持的最大连接数, 即全连接队列的长度. 服务端收到客户端ACK后
        就处于ESTABLISHED状态, 这个TCP连接就会加入全连接队列. accept()从这个队列里取连接
        listen() marks the socket referred to by sockfd as a passive socket, that is, as a socket
        that will be used to accept incoming connection requests using accept(2).
        The sockfd argument is a file descriptor that refers to a socket of type  SOCK_STREAM  or
        SOCK_SEQPACKET.

        The backlog argument defines the maximum length to which the queue of pending connections
        for sockfd may grow.  If a connection request arrives when the queue is full, the  client
        may  receive  an  error with an indication of ECONNREFUSED or, if the underlying protocol
        supports retransmission, the request may be ignored so that a later reattempt at  connec‐
        tion succeeds.
     */

    /*
        typedef union epoll_data
        {
        void *ptr;
        int fd;
        uint32_t u32;
        uint64_t u64;
        } epoll_data_t;

        struct epoll_event
        {
        uint32_t events;	/* Epoll events 
        epoll_data_t data;	/* User data variable 
        } __EPOLL_PACKED;
    */
    // IO multiplexing, epoll(), events array
    epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);
    /*
        int epoll_create(int size); 
        返回一个指向epoll instance的文件描述符, 用于后续所有对该epoll的调用
        当指向一个epoll instance的文件描述符全部被close()之后, 该epoll instance就会被释放
        epoll_create() returns a file descriptor referring to the new epoll instance.  This file  descriptor  is  used
        for all the subsequent calls to the epoll interface.  When no longer required, the file descriptor returned by
        epoll_create() should be closed by using close(2).  When all file descriptors referring to an  epoll  instance
        have been closed, the kernel destroys the instance and releases the associated resources for reuse.
    */

    // 设置epoll监听listen_fd上的EPOLLIN事件
    addfd(epoll_fd, listen_fd, false);
    /* 
        int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
        控制epoll实例epfd, 将event施加到fd指向的文件描述符
        This  system  call  performs  control  operations  on the epoll(7) instance
        referred to by the file descriptor epfd.  It requests that the operation op
        be performed for the target file descriptor, fd.
        The event argument describes the object linked to the file  descriptor  fd.
     */
    http_connection::m_epoll_fd = epoll_fd;
    /*
        同步I/O模拟的proactor模式流程:
        
            主线程往epoll内核事件表注册socket上的读就绪事件。

            主线程调用epoll_wait等待socket上有数据可读

            当socket上有数据可读，epoll_wait通知主线程,主线程从socket循环读取数据，直到没有更多数据可读，
            然后将读取到的数据封装成一个请求对象并插入请求队列。

            睡眠在请求队列上某个工作线程被唤醒，它获得请求对象并处理客户请求，然后往epoll内核事件表中注册
            该socket上的写就绪事件

            主线程调用epoll_wait等待socket可写。

            当socket上有数据可写，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果。
      */
    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        /* 
            int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
            阻塞. 等待epoll实例上注册的事件发生, epoll回把就绪事件写到events中. epoll_wait()最多
            返回maxevents个事件
            timeout == -1 表示一直阻塞直到事件发生
            timeout == 0 表示不阻塞, epoll_wait()立即返回
            
            The  epoll_wait()  system  call  waits  for events on the epoll(7) instance
            referred to by the file descriptor epfd.  The memory  area  pointed  to  by
            events  will  contain the events that will be available for the caller.  Up
            to maxevents are returned by epoll_wait().  The maxevents argument must  be
            greater than zero.

            The timeout argument specifies the number of milliseconds that epoll_wait()
            will block.  Time is measured against the CLOCK_MONOTONIC clock. 
            Specifying a timeout of -1 causes epoll_wait()
            to block indefinitely, while specifying  a  timeout  equal  to  zero  cause
            epoll_wait() to return immediately, even if no events are available.
            The  call will block until either:

            *  a file descriptor delivers an event;

            *  the call is interrupted by a signal handler; or

            *  the timeout expires.
         */
        if (n < 0 && errno != EINTR) {
            printf("epoll failed\n");
            break;
        }

        // iterate over events array
        for (int i = 0; i < n; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listen_fd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connection_fd = accept(listen_fd, (struct sockaddr*)&client_address, &client_addrlen);
                /* 
                    int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
                    accept()系统调用用于面向连接的socket, 它从监听socket sockfd的全连接队列
                    中取出第一个连接请求, 创建一个已连接的socket并返回相应的文件描述符.
                    监听socket, sockfd, 不受accept()影响

                    accept()会将客户端socket信息保存到 addr 中

                    如果全连接队列为空, 且sockfd未设置非阻塞, accept()将阻塞调用者,
                    直到有新的链接. 如果sockfd设置为nonblocking 且队列为空, accept()
                    失败, 错误码为 EAGAIN 或 EWOULDBLOCK

                    使用select, poll, epoll来接收新连接到达通知
                    当新连接到达时, epoll会发送一个可读事件, 然后再调用accept()来为该
                    连接建立socket

                    In  order  to  be  notified  of incoming connections on a
                    socket, you can use select(2), poll(2), or  epoll(7).   A
                    readable event will be delivered when a new connection is
                    attempted and you may then call accept() to get a  socket
                    for  that  connection.   Alternatively,  you  can set the
                    socket to deliver SIGIO when activity occurs on a socket

                    The  accept()  system  call is used with connection-based
                    socket types (SOCK_STREAM, SOCK_SEQPACKET).  It  extracts
                    the first connection request on the queue of pending con‐
                    nections for the listening socket, sockfd, creates a  new
                    connected  socket,  and  returns  a  new  file descriptor
                    referring to that socket.  The newly  created  socket  is
                    not  in  the listening state.  The original socket sockfd
                    is unaffected by this call
                 */
                if (connection_fd < 0) {
                    printf("errno == %d\n", errno);
                    continue;
                }
                if (http_connection::m_user_count >= MAX_FD) {
                    close(connection_fd);
                    continue;
                }
                // 
                users[connection_fd].init(connection_fd, client_address);

            } else if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                // 对方异常断开或错误等事件, 关闭连接
                // 将该连接对应的fd从epoll instance中移除
                // 关闭该文件描述符
                // 将该http_connection实例中的m_sockfd置为-1
                users[sockfd].close_connection();
                // 
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) { // 一次性读完数据
                    pool->append(users + sockfd);
                } else {
                    // 
                    users[sockfd].close_connection();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) { // 一次性写完数据
                    users[sockfd].close_connection();
                }
            }
        }
    }
    close(epoll_fd);
    close(listen_fd);

    delete[] users;
    delete pool;

    return 0;





}