#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: jdbhttpd/0.1.0\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

// 错误处理
void error_die(const char* str) {
    perror(str);
    exit(1);
}



// 初始化httpd服务，包括建立套接字，绑定端口，进行监听等
int startup(unsigned short* port) {
    // 创建套接字
    int httpd = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd == -1) {
        error_die("socket ERROR");
    }
    // // 设置选项
    // int on = 1;
    // if (setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    //     error_die("setsockopt ERROR");
    // }
    // 套接字绑定
    struct sockaddr_in info;
    info.sin_family = AF_INET;
    info.sin_port = htons(*port);
    info.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr*)&info, sizeof(info)) < 0) {
        error_die("bind ERROR");
    }
    // 无默认端口则动态分配一个
    if (*port == 0) {
        int infolen = sizeof(info);
        if (getsockname(httpd, (struct sockaddr*)&info, (socklen_t*)&infolen) == -1) {
            error_die("getsockname");
        }
        *port = ntohs(info.sin_port);
    }
    // 进入监听
    if (listen(httpd, 5) < 0) {
        error_die("listen ERROR");
    }
    return httpd;
}

// 解析一行http报文
int get_line(int sock, char* buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

// 未定义请求
void unimplemented(int client) {
    char buf[1024];
    // 发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

// 找不到请求的资源链接
void not_found(int client) {
    char buf[1024];
    // 返回404
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

// cgi运行失败
void cannot_execute(int client) {
    char buf[1024];
    //发送500
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

// 无效请求
void bad_request(int client) {
    char buf[1024];
    //发送400
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

void headers(int client) {
    char buf[1024];
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE* fp) {
    char buf[1024];
    fgets(buf, sizeof(buf), fp);
    while (!feof(fp)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), fp);
    }
}

void serve_file(int client, const char* filename) {
    char buf[1024];
    int numchars = 1;
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));
    FILE* fp = fopen(filename, "r");
    if (fp == NULL) {
        not_found(client);
    }
    else {
        // 添加http头部
        headers(client);
        // 发送文件内容
        cat(client, fp);
    }
    fclose(fp);
}

// 执行cgi动态解析
void execute_cgi(int client, const char* path, const char* method, const char* get_parameter) {
    int cgi_input[2], cgi_output[2]; // 声明cgi文件的读写管道
    int content_length = -1;
    char buf[1024];
    int len = 1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0) {
        while ((len > 0) && strcmp("\n", buf))  /* read & discard headers */
            len = get_line(client, buf, sizeof(buf));
    }
    else if (strcasecmp(method, "POST") == 0) {  // POST
        len = get_line(client, buf, sizeof(buf));
        while (len > 0 && strcmp(buf, "\n")) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0) {
                // 获取Content-Length的值
                content_length = atoi(&(buf[16]));
            }
            len = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }

    // 使用管道
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }
    // fork一个进程
    pid_t pid = fork();
    if (pid < 0) {
        cannot_execute(client);
        return;
    }

    // 返回正确响应码
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if (pid == 0) {
        // 子进程：运行CGI脚本
        char meth_env[255];
        char get_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);  // 将系统标准输出重定向为cgi_output[1]
        dup2(cgi_input[0], STDIN);    // 将系统标准输入重定向为cgi_input[0]，这一点非常关键
        close(cgi_output[0]);         // 关闭了cgi_output中的读通道
        close(cgi_input[1]);          // 关闭了cgi_input中的写通道
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            // 存储get方法的参数
            sprintf(get_env, "QUERY_STRING=%s", get_parameter);
            putenv(get_env);
        }
        else {   /* POST */
            // 存储CONTENT_LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL); //执行CGI脚本
        exit(0);
    }
    else {
        // 父进程
        close(cgi_output[1]);  // 和子进程相反，关闭了cgi_output中的写通道
        close(cgi_input[0]);   // 关闭了cgi_input中的读通道
        int i = 0;
        char c = 'A';
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {
                // 读取POST中的内容
                recv(client, &c, 1, 0);
                // 将数据发送给子进程处理
                write(cgi_input[1], &c, 1);
            }
        }

        // 通过管道读取子进程传输过来的返回页面
        while (read(cgi_output[0], &c, 1) > 0) {
            // 发送给浏览器
            send(client, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);

        // 父进程等到子进程结束，自己再结束
        int status;
        waitpid(pid, &status, 0);
        // waitpid(pid, NULL, 0);
    }

}

// 接受client的连接，并读取请求信息
void accept_request(void* arg) {
    int client = (intptr_t)arg;
    char buf[1024], path[1024];
    int cgi_tag = 0;

    // 获取报文的第一行信息
    int len = get_line(client, buf, sizeof(buf));
    printf("获取的请求头: %s\n", buf);
    // 解析报文头部信息，格式为 <method> <url> <version>
    // 解析method
    char method[512], url[512];
    int p1 = 0, p2 = 0;
    while (p1 < len && buf[p1] == ' ') p1++;
    while (p1 < len && buf[p1] != ' ') {
        method[p2] = buf[p1];
        p1++;p2++;
    }
    method[p1] = '\0';
    // 只实现了GET和POST请求，其他请求特判掉
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);
        return;
    }
    else if (strcasecmp(method, "POST") == 0) {
        cgi_tag = 1;
    }
    // 解析url
    while (p1 < len && buf[p1] == ' ') p1++;
    p2 = 0;
    while (p1 < len && buf[p1] != ' ') {
        url[p2] = buf[p1];
        p1++;p2++;
    }
    url[p2] = '\0';
    // 注意一下如果请求方式是GET的话，url可能是 url+?+<parameter> 的格式
    char* get_parameter = NULL;
    if (strcasecmp(method, "GET") == 0) {
        get_parameter = url;
        while (*get_parameter != '?' && *get_parameter != '\0') get_parameter++;
        // 带parameter
        if (*get_parameter == '?') {
            cgi_tag = 1;
            *get_parameter = '\0';
            get_parameter++;
        }
    }
    // 将url中的路径格式化到path
    sprintf(path, "htdocs%s", url);

    // 如果path只是一个目录，默认设置为首页index.html
    if (path[strlen(path) - 1] == '/') {
        strcat(path, "index.html");
    }
    struct stat st;
    // 根据资源path把文件信息并存储到结构体st中
    if (stat(path, &st) == -1) {
        not_found(client);
    }
    else {
        printf("找到资源文件: %s\n", path);
        // S_IXUSR:文件所有者具可执行权限
        // S_IXGRP:用户组具可执行权限
        // S_IXOTH:其他用户具可读取权限 
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {
            cgi_tag = 1;
        }

        if (!cgi_tag) {
            printf("GO serve_file\n");
            // 将静态文件返回
            serve_file(client, path);
        }
        else {
            printf("GO execute_cgi\n");
            // 执行cgi动态解析
            execute_cgi(client, path, method, get_parameter);
        }
    }

    close(client);
}



int main() {
    // 这里定义一些变量
    unsigned short port = 4000;
    struct sockaddr client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    // 根据port(IP默认本机)创建服务器socket
    int server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    // 服务程序
    while (1) {
        // accept一个client套接字
        int link_sock = accept(server_sock, &client_name, &client_name_len);
        if (link_sock == -1) {
            error_die("accept ERROR");
        }
        else {
            printf("和一个client建立连接...\n");
        }

        // 创建一个线程让其执行任务
        if (pthread_create(&newthread, NULL, (void*)accept_request, (void*)(intptr_t)link_sock) != 0) {
            error_die("pthread_create ERROR");
        }
    }

    return 0;
}