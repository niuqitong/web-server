#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>


class http_connection {
public:

    // 所有socket上的事件都被注册到同一个epoll上
    static int m_epoll_fd; 
    static int m_user_count;

    static const int  READ_BUFFER_SIZE = 2048;
    static const int  WRITE_BUFFER_SIZE = 1024;
    

    http_connection(){}

    ~http_connection(){}
    // initialize the newly-built connection
    void init(int sockfd, const sockaddr_in& addr);
    void process(); // handle requests from clients
    void close_connection();
    bool read(); // nonblocking
    bool write();

private:
    int m_sockfd; // socket of this http connection
    sockaddr_in m_address; // socket address of the communication
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_index;

};

#endif