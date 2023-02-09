#include "http_connection.h"

int http_connection::m_epoll_fd = -1;
int http_connection::m_user_count = 0;

// 设置文件描述符非阻塞
int16_t setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, old_flag | O_NONBLOCK);
    return old_flag;
}

// 将fd指定的连接的读事件注册到epoll_fd指定的epoll instance
// 并将fd设置为非阻塞
void addfd(int epoll_fd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; // EPOLLRDHUO 在底层处理对端异常断开
    /* 
        EPOLLRDHUP (since Linux 2.6.17)

            Stream  socket  peer closed connection, or shut down writing half of con‐
            nection.  (This flag is especially useful  for  writing  simple  code  to
            detect peer shutdown when using Edge Triggered monitoring.) 

        EPOLLONESHOT (since Linux 2.6.2)

              Sets the one-shot behavior for  the  associated  file  descriptor.   This
              means that after an event is pulled out with epoll_wait(2) the associated
              file descriptor is internally  disabled  and  no  other  events  will  be
              reported  by  the  epoll  interface.  The user must call epoll_ctl() with
              EPOLL_CTL_MOD to rearm the file descriptor with a new event mask.
    */
    // event.events = EPOLLIN | EPOLLRDHUP; 
    if (one_shot) {
        event.events |= EPOLLONESHOT;
        // 只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，
        // 需要再次把这个socket加入到EPOLL队列里
        // epoll则可以工作在ET高效模式，并且epoll还支持EPOLLONESHOT事件，该事件能进一步
        // 减少可读、可写和异常事件被触发的次数。
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);

}

// 更新fd oneshot
void modifyfd(int epoll_fd, int fd, int e) {
    epoll_event event;
    event.data.fd = fd;
    event.events = e | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

// sockfd: 新建立的连接的sockfd
// addr: 客户端socket 信息, accept()时传入一个struct sockaddr* addr, 
//       由accept()将客户端socket信息保存到addr
void http_connection::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 将新连接读事件注册到epoll instance
    addfd(m_epoll_fd, sockfd, true);
    m_user_count++;
    // init();
}


void http_connection::close_connection() {
    if (m_sockfd != -1) {
        removefd(m_epoll_fd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_connection::read() {
    printf("read\n");
    if (m_read_index >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        /* 
            ssize_t recv(int sockfd, void *buf, size_t len, int flags);

            recv() 从 sockfd 接收信息, 可用于 TCP, UDP. 返回接收到的数据长度
            如果sockfd上没有数据:
                if sockfd被设为nonblocking:
                    返回 -1, errno 设置为 EAGAIN 或 EWOULDBLOCK
                else:
                    recv()阻塞等待数据可用

            sockfd一旦有数据可接收, recv()就会接收并返回收到的数据长度 最多可从sockfd 
            读 len 大小的数据. 当连接对方关闭连接后, 返回0, 表示 end-of-file
            The  recv(),  recvfrom(), and recvmsg() calls are used to receive
            messages from a socket.  They may be used to receive data on both
            connectionless  and connection-oriented sockets.

            All  three  calls  return the length of the message on successful
            completion.  If a message is too long to fit in the supplied buf‐
            fer,  excess  bytes  may  be  discarded  depending on the type of
            socket the message is received from. 
            When  a stream socket peer has performed an orderly shutdown, the
            return value will be 0 (the traditional "end-of-file" return).

            If no messages are available at the  socket,  the  receive  calls
            wait  for  a  message to arrive, unless the socket is nonblocking
            (see fcntl(2)), in which case the value -1 is  returned  and  the
            external  variable  errno  is  set to EAGAIN or EWOULDBLOCK.  The
            receive calls normally return  any  data  available,  up  to  the
            requested  amount,  rather  than  waiting for receipt of the full
            amount requested.

            The only difference between recv() and read(2) is the presence of
            flags.  With a zero flags argument, recv() is  generally  equiva‐
            lent to read(2)

            ssize_t read(int fd, void *buf, size_t count);
            read() 返回0时表示读到EOF
         */
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data
                break;
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_index += bytes_read;
    }
    printf("data read: %s", m_read_buf);
    return true;
}

bool http_connection::write() {
    printf("write\n");
    return true;
}

// 由线程池中的线程调用，处理HTTP请求的入口函数
void http_connection::process() {

    // 解析HTTP请求
    printf("parsing request, creating response\n");

    // 生成响应，准备好数据
    
}