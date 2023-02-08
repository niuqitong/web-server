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
    // event.events = EPOLLIN | EPOLLRDHUP; 
    if (one_shot) {
        event.events |= EPOLLONESHOT;
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