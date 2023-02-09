#include "http_connection.h"

int http_connection::m_epoll_fd = -1;
int http_connection::m_user_count = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "./resources";

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

void http_connection::init() {
    bytes_to_send = 0;
    bytes_sent = 0;
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_index = 0;
    m_write_idx = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

void http_connection::close_connection() {
    if (m_sockfd != -1) {
        removefd(m_epoll_fd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_connection::read() {
    // printf("read\n");
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


// 解析一行，判断依据\r\n
http_connection::LINE_STATUS http_connection::parse_line() {
    char temp;
    for ( ; m_checked_idx < m_read_index; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_index ) {
                return LINE_OPEN;
            } else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_connection::HTTP_CODE http_connection::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_connection::HTTP_CODE http_connection::parse_headers(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_connection::HTTP_CODE http_connection::parse_content( char* text ) {
    if ( m_read_index >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求
http_connection::HTTP_CODE http_connection::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state ) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

http_connection::HTTP_CODE http_connection::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    /* 
        int stat(const char *pathname, struct stat *statbuf);
        return information about a  file,  in  the  buffer pointed  to 
        by statbuf.
     */


    if (!(m_file_stat.st_mode) & S_IROTH) // st_mode: file type and mode
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    // st_size: total size of the file in bytes
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    /* 
        void *mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset);
        mmap()在当前进程的内存空间创建一个新的内存映射, 映射的起始虚拟地址为
        addr(addr为0表示由内核选择映射的起始地址), 长度为length, 返回创建的
        映射的起始地址. 文件映射的内容将初始化为fd offset 处开始的 length 个
        字节. prot指定该映射的权限, PROT_READ表示可读. flags用来指定对该映射
        内存的写操作如何影响其他进程和相应文件, MAP_PRIVATE表示创建一个写时拷
        贝映射, 对该区域的写操作对其他进程不可见, 且不会更新到相应文件
        
        offset必须为页大小的整数倍
        若addr不为0, 内核会在addr附近的page-aligned处创建映射

        On error, the value MAP_FAILED (that is,  (void *) -1)  is  returned,
        and errno is set to indicate the cause of the error.

        mmap()  creates  a new mapping in the virtual address space of the
        calling process.  The starting address  for  the  new  mapping  is
        specified  in  addr.   The length argument specifies the length of
        the mapping (which must be greater than 0).

        prot
                PROT_EXEC  Pages may be executed.

                PROT_READ  Pages may be read.

                PROT_WRITE Pages may be written.

                PROT_NONE  Pages may not be accessed.

        flags
                MAP_SHARED
                    Share this mapping.  Updates to the mapping are visible  to
                    other  processes  mapping the same region, and (in the case
                    of file-backed mappings) are carried through to the  under‐
                    lying file.  (To precisely control when updates are carried
                    through  to  the  underlying  file  requires  the  use   of
                    msync(2).)

                MAP_PRIVATE
                    Create  a  private  copy-on-write  mapping.  Updates to the
                    mapping are not visible to other processes mapping the same
                    file,  and  are not carried through to the underlying file.
                    It is unspecified whether changes made to  the  file  after
                    the mmap() call are visible in the mapped region.
        
     */
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区进行munmap
void http_connection::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        /* 
            The munmap() system call deletes the mappings  for  the  specified
            address  range,  and causes further references to addresses within
            the range to generate invalid memory references.   The  region  is
            also  automatically  unmapped  when the process is terminated.  On
            the other hand, closing the file descriptor  does  not  unmap  the
            region.

            The  address  addr must be a multiple of the page size (but length
            need not be).  All pages containing a part of the indicated  range
            are unmapped, and subsequent references to these pages will gener‐
            ate SIGSEGV.  It is not an error if the indicated range  does  not
            contain any mapped pages.

            On  success,  munmap()  returns 0.  On failure, it returns -1, and
            errno is set to indicate the cause of the error (probably to  EIN‐
            VAL).
         */
        m_file_address = 0;
    }
}

bool http_connection::write() {
    // printf("write\n");
    int temp = 0;
    // int bytes_sent = 0; // 已经发送的字节数
    // int bytes_to_send = m_write_idx; // 写缓冲区中待发送的字节数

    if (bytes_to_send == 0) {
        // 无待发送字节，响应结束
        modifyfd(m_epoll_fd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        /* 
            将多个(2, in our case)buffer内的数据写到sockfd的缓冲区中
            ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

            The  writev()  system call writes iovcnt buffers of data described
            by iov to the file associated with the file descriptor fd ("gather
            output"). The writev() system call works just like write(2) except 
            that multiple buffers are written out.

            writev() writes out the entire contents of iov[0] before proceeding 
            to iov[1], and so on.

            On success, return  the  number of bytes written.
            On error, -1 is returned, and errno is set appropriately.

            The pointer iov points to an array of iovec structures, defined in
            <sys/uio.h> as:

                struct iovec {
                    void  *iov_base;    /* Starting address 
                    size_t iov_len;     /* Number of bytes to transfer 
                };
         */
        if (temp <= -1) {
            if (errno == EAGAIN) {
                modifyfd(m_epoll_fd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_sent += temp;
        
        if (bytes_sent >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_sent - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_sent;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            unmap();
            modifyfd(m_epoll_fd, m_sockfd, EPOLLIN);
            // 根据HTTP请求中的keepalive设置
            if (m_linger) {
                init();
                return true;
            } else return false;
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_connection::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, 
                    WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    /* 
        int vsnprintf(char *str, size_t size, const char *format, va_list ap);

        The  functions  vprintf(),  vfprintf(),  vdprintf(),   vsprintf(),
        vsnprintf()  are  equivalent to the functions printf(), fprintf(),
        dprintf(), sprintf(), snprintf(), respectively, except  that  they
        are  called  with  a va_list instead of a variable number of argu‐
        ments.  These functions do not call  the  va_end  macro.   Because
        they  invoke  the va_arg macro, the value of ap is undefined after
        the call.

        All of these functions write the output under  the  control  of  a
        format  string  that  specifies how subsequent arguments (or argu‐
        ments accessed via  the  variable-length  argument  facilities  of
        stdarg(3)) are converted for output.
        
        返回值
        The functions snprintf() and vsnprintf() do not  write  more  than
        size  bytes  (including the terminating null byte ('\0')).  If the
        output was truncated due to this limit, then the return  value  is
        the  number  of  characters  (excluding the terminating null byte)
        which would have been written to the final string if enough  space
        had  been  available.   Thus, a return value of size or more means
        that the output was truncated.  (See also below under NOTES.)

       If an output error is encountered, a negative value is returned.
     */
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_connection::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_connection::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_connection::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_connection::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_connection::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_connection::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_connection::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_connection::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 由线程池中的线程调用，处理HTTP请求的入口函数
void http_connection::process() {

    // 解析HTTP请求
    // printf("parsing request, creating response\n");
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modifyfd(m_epoll_fd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        close_connection();
    modifyfd(m_epoll_fd, m_sockfd, EPOLLOUT);

    // 生成响应，准备好数据
    
}