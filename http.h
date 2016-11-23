#ifndef _HTTP_H
#define _HTTP_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<stdio.h>
#include<unistd.h>
#include"bloom.h"
#include<sys/types.h>
#include<sys/epoll.h>
#include<queue>
#include<sys/stat.h>
#include<fcntl.h>

#include"url.h"

int connect_pending = 0;
pthread_mutex_t connlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t funclock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	Url *url;
	int fd;
} Ev_arg;

extern queue < Url * >url_queue;
extern int epfd, line;
extern struct epoll_event ev;
extern struct epoll_event events[10];
extern pthread_mutex_t queue_lock;
extern FILE *result;

/*设置文件描述符为非阻塞模式*/
void setnoblocking(int sockfd)
{
	int opts;
	opts = fcntl(sockfd, F_GETFL);	//获取文件标志和访问模式
	if (opts < 0) {
		perror("fcntl(sockfd,GETFL)");
		exit(1);
	}
	opts |= O_NONBLOCK;	//非阻塞
	if (fcntl(sockfd, F_SETFL, opts) < 0) {
		perror("fcntl(sockfd,SETFL)");
		exit(1);
	}
}

int buildConnect(int *client_sockfd, char *ip)
{
	/*创建服务器套接口地址 */
	struct sockaddr_in server_address;
	bzero(&server_address, sizeof(struct sockaddr_in));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(80);
	if (inet_aton(ip, &(server_address.sin_addr)) == 0) {	//将点分十进制形式转换为套接口内部数据类型,将字符串型的ip转为32位地址
		perror("inet_pton");
		printf("ip=%s\n", ip);
		return -1;
	}
	if ((*client_sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}
	/*连接到服务器 */
	if (connect (*client_sockfd, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in)) == -1) {
		close(*client_sockfd);
		perror("connect");
		return -1;
	}
	pthread_mutex_lock(&connlock);
	connect_pending++;
	pthread_mutex_unlock(&connlock);
	return 0;
}

/*发送http request*/
int sendRequest(Url * url, int fd)
{
	char request[1024] = { 0 };
	sprintf(request, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", url->path, url->domain);
	printf("GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", url->path, url->domain);
	int need = strlen(request);	/*还需要向sockfd中写入这么多的数据 */
	int tmp = 0;		/*记录已发送的数据量 */
	int n;			//记录读写的实际字节数
	while (need > 0) {
		n = write(fd, request + tmp, need);
		if (n < 0) {
			if (errno == EAGAIN) {	/*写缓冲队列已满，延时后重试 */
				usleep(1000);
				continue;
			}
			freeUrl(url);	/*如果是其他错误则返回-1,表示发送失败,同时释放url */
			close(fd);
			return -1;
		}
		need -= n;
		tmp += n;
	}
	//fprintf(stderr,"发送http请求成功==>%s%s\n",url->host,url->resource);
	return 0;
}

/*获取html response，并写入文件.同时抽取超链接，放入urk队列*/
void *recvResponse(Ev_arg *arg)
{
	Url *url = arg->url;
	int linetmp = 0;
    
    fprintf(result, "line %d: url = ", line++);
    printf("line %d: url = %s\n", line,arg->url->path);
	linetmp = line;
    fprintf(result, url->domain);
    fprintf(result, url->path);
    fprintf(result, "\n");
	int fd = arg->fd;
	char *fn = link2fn(url);	/*以url作为文件名，斜线转换为下划线 */
	int htmlfd = open(fn, O_WRONLY | O_CREAT | O_APPEND, 0644);	/*以只写方式打开html文件 */

	if (htmlfd < 0) {
		fprintf(stderr, "函数recvResponse()中%s文件打开失败\n%s\t%s\n", fn, url->domain, url->path);
		//freeUrl(url);
		close(fd);
			printf("url exists : %s \n",url->path);
		return NULL;
	}
	free(fn);

	printf("开始下载fd=%d, url=%s%s \n",fd,url->domain,url->path);
	int i, n, need, ll = 0;
	char buf[10000] = { 0 };
	while (1) {
		need = sizeof(buf) - 1 - ll;
		n = read(fd, buf, need);
		if (n < 0) {
			if (errno == EAGAIN) {
				usleep(1000);
				continue;
			} else {
				fprintf(stderr,
					"读取http响应发生错误\n");
				//freeUrl(url);
				close(htmlfd);
				return NULL;
			}
		} else if (n == 0) {	/*读取http响应这完毕 */
			break;
		} else {	/*还需要继续读 */
			//printf("从socket中读出:|%s|\n",buf);
			write(htmlfd, buf, n);
			ll = extractLink(buf, url->domain, url);
		}
	}
	close(htmlfd);
	//fprintf(stderr,"获取http响应成功==>%s%s\n",url->host,url->resource);
	//freeUrl(url);
	return NULL;
}

#endif
