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
    // if (listen_fd == -1)

    // 设置端口复用 
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // bind
    struct sockaddr_in address; 
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int ret = 0;
    ret = bind(listen_fd, (struct sockaddr*)&address, sizeof(address));

    // listen
    ret = listen(listen_fd, 5);

    // IO multiplexing, epoll(), events array
    epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);

    addfd(epoll_fd, listen_fd, false);

    http_connection::m_epoll_fd = epoll_fd;

    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
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
                //  对方异常断开或错误等事件
                users[sockfd].close_connection();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) { // 一次性读完数据
                    pool->append(users + sockfd);
                } else {
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