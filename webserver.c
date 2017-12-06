#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h> 
#include <sys/types.h>  
#include <unistd.h> 
#include <string.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <sys/shm.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

#define SERVER_ADDR "127.0.0.1" //服务器地址
#define SERVER_PORT 2238 //端口号
#define LISTEN_QUEUE 20 //侦听队列长度
#define RECV_SIZE 1024 //recv函数每次接收的字节数
#define RECV_BUFFER_SIZE 4096 //存储接收的数据包的缓冲区字节数
#define SEND_BUFFER_SIZE 65536 //存储发送的数据包的缓冲区的字节数
#define URL_BUFFER_SIZE 128 //存储请求头中的url的缓冲区的字节数
#define TRUE 1
#define FALSE 0
#define GET 1 //GET方法
#define POST 2 //POST方法
#define HTML 1
#define JPG 2
#define TXT 3

extern int errno;

struct ClientRecord{ //客户端信息
    int client_id; //客户端编号
    int connected_socket; //客户端socket连接句柄
    uint16_t sin_port; //客户端端口号
    uint32_t s_addr; //客户端ip地址
    pthread_t tid; //负责处理该客户端连接的子线程id
    struct ClientRecord *next;
};

struct ClientList{ //客户端列表
    struct ClientRecord *head, *tail;
    int client_num; //已连接的客户端数量
    int id_counter; //客户端编号计数器，用于记录客户端编号已经编到了哪个数
};

struct ClientList client_list; //当前连接的客户端列表
int listening_socket, connected_socket; //服务端侦听句柄和连接句柄
pthread_mutex_t mutex_list;

void *service(void *para);
void init_client_list(struct ClientList *client_list);
struct ClientRecord *init_client_record(int connected_socket, struct sockaddr_in *client_addr);
void add_client(struct ClientList *client_list, struct ClientRecord *client_record);
void drop_client(struct ClientList *client_list, struct ClientRecord *client_record);
struct ClientRecord *search_client(struct ClientList *client_list, int client_id);
void exit_handler(int sign);

char *http_response_head = "HTTP/1.1 %d OK\r\n"
    "Server: WebServer\r\n"
    "Accept-Ranges: bytes\r\n"
    "Content-Length: %ld\r\n"
    "Connection: close\r\n"
    "Content-Type: %s\r\n\r\n";

int main(int argc, char **argv){
    signal(SIGINT, exit_handler);
    socklen_t client_length;
    struct sockaddr_in client_addr, server_addr; //客户端地址信息和服务端地址信息
    struct ClientRecord *client_record;
    const void *on;

    pthread_mutex_init(&mutex_list, NULL);
    //创建侦听句柄
    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(listening_socket == -1){
        printf("Fail: socket() Code: %d\n", errno);
        return -1;
    }
    //设置socket地址端口重用
    if(setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){  
        printf("Fail: setsockopt() Code: %d\n", errno);
        return -1;
    } 
    //设置服务端地址信息
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    server_addr.sin_port = htons(SERVER_PORT);
    bzero(&(server_addr.sin_zero), 8);
    //绑定服务端地址
    if(bind(listening_socket, (struct sockaddr *)(&server_addr), sizeof(server_addr)) == -1){
        printf("Fail: bind() Code: %d\n", errno);
        close(listening_socket);
        return -1;
    }
    //侦听客户端连接请求
    if(listen(listening_socket, LISTEN_QUEUE) == -1){
        printf("Fail: listen() Code: %d\n", errno);
        close(listening_socket);
        return -1;
    }
    printf("服务器运行中\nCtrl+C终止运行\n");

    init_client_list(&client_list); //初始化客户端列表
    while(1){
        connected_socket = accept(listening_socket, (struct sockaddr *)(&client_addr), &client_length); //接收客户端连接请求
        if(connected_socket == -1){ //创建连接句柄失败
            printf("Fail: accept Code: %d\n", errno);
        }
        else{
            pthread_mutex_lock(&mutex_list);
            client_record = init_client_record(connected_socket, &client_addr); //初始化客户端信息
            add_client(&client_list, client_record); //将客户端信息添加进客户端列表
            printf("第%d个请求\n", client_record->client_id);
            pthread_create(&(client_record->tid), NULL, service, (void *)(&(client_record->client_id))); //创建子线程
            pthread_mutex_unlock(&mutex_list);
        }
    }
}

ssize_t write_to_socket(int connected_socket, void *buffer, size_t size){
    size_t left = size;
    ssize_t ret;
    char *temp_ptr = buffer;

    while(left > 0){
        if((ret = write(connected_socket, temp_ptr, left)) <= 0){
            return -1;
        }
        left -= ret;
        temp_ptr += ret;
    }
    return size;
}

//对get请求的响应
void responce_for_get(int connected_socket, char *filepath, int filesize, int filetype, int status_code){
    int filedesc;
    char *map_area_ptr, send_buffer[SEND_BUFFER_SIZE];

    //给客户端发送响应报头
    if(filetype == HTML){
        sprintf(send_buffer, http_response_head, status_code, filesize, "text/html");
    }
    else if(filetype == JPG){
        sprintf(send_buffer, http_response_head, status_code, filesize, "image/jpeg");
    }
    else if(filetype == TXT){
        sprintf(send_buffer, http_response_head, status_code, filesize, "text/plain");
    }
    write_to_socket(connected_socket, send_buffer, strlen(send_buffer));

    if(status_code == 200){
        filedesc = open(filepath, O_RDONLY, 0); //打开文件
        map_area_ptr = mmap(0, filesize, PROT_READ, MAP_PRIVATE, filedesc, 0); //将存储文件的内存映射到虚拟地址空间
        close(filedesc); //关闭文件
        write_to_socket(connected_socket, map_area_ptr, filesize);
        munmap(map_area_ptr, filesize); //取消映射
    }
}

//对post请求的响应
void responce_for_post(int connected_socket, int is_valid){
    char send_buffer[SEND_BUFFER_SIZE];
    char *success_str = "<html><body>登录成功</body></html>";
    char *fail_str = "<html><body>登录失败</body></html>";
    char *temp_str;

    if(is_valid == TRUE){
        temp_str = success_str;
    }
    else{
        temp_str = fail_str;
    }
    sprintf(send_buffer, http_response_head, 200, strlen(temp_str), "text/html;charset=utf-8");
    sprintf(send_buffer, "%s%s\r\n", send_buffer, temp_str);
    write_to_socket(connected_socket, send_buffer, strlen(send_buffer));
}

//接收数据包
void receive(int connected_socket, char *recv_buffer){
    char *temp_ptr = recv_buffer;
    int ret;
    
    memset(temp_ptr, 0, RECV_BUFFER_SIZE);
    while(1){
        ret = recv(connected_socket, temp_ptr, RECV_SIZE, 0);
        if(ret > 0){
            if(ret != RECV_SIZE){ //本次循环实际读取的数据大小小于RECV_BUFFER_SIZE，数据包已经读取完毕
                break;
            }
            else{ //本次循环实际读取的数据大小等于RECV_BUFFER_SIZE，数据包可能还未读取完毕，需要继续读取
                temp_ptr = (char *)(temp_ptr + RECV_SIZE);
            }
        }
        else if(ret == 0){
            continue;
        }
        else{
            printf("Fail: recv() Code: %d\n", errno);
            break;
        }
    }
}

//判断数据包是否为HTTP请求数据包
int is_http_request(char *recv_buffer){
    if(strstr(recv_buffer, "\r\n\r\n") == NULL){
        return FALSE; //不是HTTP的请求数据包
    }
    return TRUE;
}

//从请求头中解析出请求方法和url
int parse_requset_head(char *recv_buffer, char *url_buffer){
    int i, first_blank = -1, second_blank = -1, length, method;

    memset(url_buffer, 0, URL_BUFFER_SIZE);
    for(i = 0; i < RECV_BUFFER_SIZE; i++){
        if(recv_buffer[i] == ' '){
            if(first_blank == -1){ //第一次出现空格，之前的字段为请求方法
                first_blank = i;
            }
            else{ //第二次出现空格，第一次出现空格到第二次出现空格之间的字段为url
                second_blank = i;
                break;
            }
        }
    }
    if(first_blank == 3){
        method = GET;
    }
    else{
        method = POST;
    }
    memmove(url_buffer, recv_buffer + first_blank + 1, second_blank - first_blank - 1);
    return method;
}

//将服务器程序的运行路径和请求头中解析出的url拼装成文件路径
void get_file_path(char *filepath, char *url_buffer){
    getcwd(filepath, URL_BUFFER_SIZE); //获取服务器当前的工作路径
    sprintf(filepath, "%s%s", filepath, url_buffer); //将工作路径和请求头中的url组装成文件路径
}

//获取文件类型
int get_file_type(char *url_buffer){
    char *str = strrchr(url_buffer, '.');

    if(strcmp(str + 1, "html") == 0){
        return HTML;
    }
    else if(strcmp(str + 1, "jpg") == 0){
        return JPG;
    }
    else if(strcmp(str + 1, "txt") == 0){
        return TXT;
    }
    return 0;
}

int valid_form(char *recv_buffer){
    char *login_start = strstr(recv_buffer, "login=");
    char *pass_start = strstr(login_start, "pass=");

    if(strncmp("3150102238", login_start + 6, 10) == 0 && login_start[16] == '&'){
        if(strncmp("2238", pass_start + 5, 4) == 0 && strlen(pass_start) == 9){
            return TRUE;
        }
    }
    return FALSE;
}

//子线程调用的函数
void *service(void *para){
    int client_id = *(int *)para; //获取当前连接的客户端id
    int connected_socket; //客户端连接句柄
    char recv_buffer[RECV_BUFFER_SIZE]; //接收数据包的缓冲区
    char url_buffer[URL_BUFFER_SIZE]; //存储请求头中url的缓冲区
    char filepath[URL_BUFFER_SIZE]; //存储文件路径的缓冲区
    struct ClientRecord *client_record;
    struct stat stat_buffer;
    int method, filetype, status_code, ret;

    client_record = search_client(&client_list, client_id); //根据id获取当前连接的客户端信息
    if(client_record == NULL){ //没有找到客户端
        printf("Client %d does not exist", client_id);
        pthread_exit(NULL);
    }
    connected_socket = client_record->connected_socket; //获取连接句柄

    while(1){
        receive(connected_socket, recv_buffer); //将收到的数据包写入recv_buffer中
        if(is_http_request(recv_buffer) == TRUE){
            if(parse_requset_head(recv_buffer, url_buffer) == GET){
                get_file_path(filepath, url_buffer);
                ret = stat(filepath, &stat_buffer);
                if(ret == -1 || S_ISDIR(stat_buffer.st_mode) != 0){
                    printf("%s cannot be found\n", filepath);
                    filetype = HTML;
                    status_code = 404;
                }
                else{
                    filetype = get_file_type(url_buffer);
                    status_code = 200;
                }
                responce_for_get(connected_socket, filepath, stat_buffer.st_size, filetype, status_code);
            }
            else{
                if(strcmp(url_buffer, "/dopost") == 0){
                    responce_for_post(connected_socket, valid_form(recv_buffer));
                }
                else{
                    responce_for_get(connected_socket, NULL, 0, filetype, 404);
                }
            }
        }
        else{
            continue;
        }
        break; //响应完毕
    }
    close(connected_socket);
    pthread_mutex_lock(&mutex_list);
    drop_client(&client_list, client_record);
    pthread_mutex_unlock(&mutex_list);
    return NULL;
}

//初始化客户端列表
void init_client_list(struct ClientList *client_list){
    client_list->head = client_list->tail = NULL;
    client_list->client_num = 0;
    client_list->id_counter = 0;
}

//初始化客户端信息
struct ClientRecord *init_client_record(int connected_socket, struct sockaddr_in *client_addr){
    struct ClientRecord *client_record = (struct ClientRecord *)malloc(sizeof(struct ClientRecord));
    client_record->connected_socket = connected_socket;
    client_record->sin_port = client_addr->sin_port;
    client_record->s_addr = client_addr->sin_addr.s_addr;
    client_record->next = NULL;
    return client_record;
}

//将客户端信息添加进客户端列表
void add_client(struct ClientList *client_list, struct ClientRecord *client_record){
    client_record->client_id = client_list->id_counter;
    client_list->id_counter += 1;
    if(client_list->client_num == 0){
        client_list->head = client_list->tail = client_record;
    }
    else{
        client_list->tail->next = client_record;
        client_list->tail = client_record;
    }
    client_list->client_num += 1;
}

//将客户端信息从客户端列表移除
void drop_client(struct ClientList *client_list, struct ClientRecord *client_record){
    struct ClientRecord *temp_ptr;
    if(client_list->head == client_record){
        if(client_list->tail == client_record){
            client_list->head = client_list->tail = NULL;
        }
        else{
            client_list->head = client_record->next;
        }
    }
    else{
        temp_ptr = client_list->head;
        while(temp_ptr->next != client_record){
            temp_ptr = temp_ptr->next;
        }
        temp_ptr->next = client_record->next;
        if(client_list->tail == client_record){
            client_list->tail = temp_ptr;
        }
    }
    free(client_record);
    client_list->client_num -= 1;
}

//在客户端列表中搜索客户端
struct ClientRecord *search_client(struct ClientList *client_list, int client_id){
    struct ClientRecord *ptr = client_list->head;
    while(ptr != NULL){
        if(ptr->client_id == client_id){
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

void exit_handler(int sign){
    struct ClientRecord *temp_ptr = client_list.head;
    while(client_list.client_num != 0){
        close(client_list.head->connected_socket);
        drop_client(&client_list, client_list.head);
    }
    close(listening_socket);
    printf("退出服务器\n");
    exit(0);
}
