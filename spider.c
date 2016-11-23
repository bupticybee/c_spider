#include<pthread.h>
#include"bloom.h"
#include"url.h"
#include"dns.h"
#include"http.h"
#include "threadpool.c"

#define MAX_QUEUE_SIZE 300
#include <cstdint>

map < string, string > host_ip_map;
queue < Url * >url_queue;
int epfd;
int line = 0;
FILE *result;
pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

void putlinks2queue(char *links[], int count);
void addurl2queue(Url * url);
int CreateThread(void *(*start_routine) (void *), void *arg, pthread_t * thread, pthread_attr_t * pAttr);


void *func(void *arg)
 {
     printf("thread %d\n", (intptr_t)arg);
	int epfd_l = epoll_create(50);	//生成用于处理accept的epoll专用文件描述符，最多监听256个事件
	struct epoll_event ev, events[10];	//ev用于注册事件，events数组用于回传要处理的事件
	Url *url;
	int n = 0,i=0;
	while(1){
		pthread_mutex_lock(&queue_lock);
		if(url_queue.size() == 0){
			pthread_mutex_unlock(&queue_lock);
			usleep(10);
			continue;
		}else{
			url = url_queue.front();
			url_queue.pop();
			pthread_mutex_unlock(&queue_lock);
		}
		//pthread_mutex_lock(&queue_lock);
		//pthread_mutex_unlock(&queue_lock);
		//printf("从url队列中读出：%s%s-->%s\n",url.domain,url.path,url.ip);
		int sockfd = -1;//尝试给下初值-1
		//printf("将要发送连接请求\n");
		int rect = buildConnect(&sockfd, url->ip);	/*发出connect请求 */
		setnoblocking(sockfd);	/*设置sockfd非阻塞模式 */
		//printf("将要发送http request\n");
		rect = sendRequest(url, sockfd);	/*发送http request */
		Ev_arg *arg = (Ev_arg *) calloc(sizeof(Ev_arg), 1);
		arg->url = url;
		arg->fd = sockfd;
		ev.data.ptr = arg;
		ev.events = EPOLLIN | EPOLLET;	//设置要处理的事件类型。可读，边缘触发
		printf("将要把sockfd=%d, url=%s%s 放入epoll\n", sockfd, url->domain, url->path);
		epoll_ctl(epfd_l, EPOLL_CTL_ADD, sockfd, &ev);	//注册ev
		while(1){
			usleep(10);
		n = epoll_wait(epfd_l, events, 10, 1000);	/*等待sockfd可读，即等待http response */
		for (i = 0; i < n; ++i) {
			Ev_arg *arg = (Ev_arg *) (events[i].data.ptr);
			printf("%d准备就绪\n",arg->fd);
		//	CreateThread(recvResponse, arg, NULL, NULL);
			recvResponse(arg);
		}
		}
	}
     return NULL;
 }

int main(int argc, char *argv[])
{
	int i, n;
	if (argc < 2) {
        printf("请按要求输入！");
		return 0;
	}
	chdir("downpage");
    result = fopen("result", "w");
	putlinks2queue(argv + 1, argc - 1);	/*把用户命令行提供的link放入待爬取url队列 */


  if(tpool_create(5) != 0) {
      printf("tpool_create failed\n");
      exit(1);
  }
  intptr_t i_w;
  for (i_w = 0; i_w < 10; ++i_w) {
      tpool_add_work(func, (void*)i_w);
  }
	while (1) {
		usleep(1000);
		//n = epoll_wait(epfd, events, 10, 0);	/*等待sockfd可读，即等待http response 返回触发事件数*/
		//for (i = 0; i < n; ++i) {
	  //	Ev_arg *arg = (Ev_arg *) (events[i].data.ptr);
		//	if (bloomDomain(arg->url->domain)) {	/*domain出现过 */
		//		if (bloomPath(arg->url->path)) {	/*path出现过，则忽略此link */
		//			//printf("fd=%d 准备就绪, n = %d, url=%s%s\n",arg->fd, n, arg->url->domain, arg->url->path);
		//			continue;
		//		}
			//CreateThread(recvResponse, arg, NULL, NULL);
		}
        //usleep(100000);
	close(epfd);
    fclose(result);
    //rename("result", "../result");
}

/*把超链接放入待爬取url队列*/
void putlinks2queue(char *links[], int count)
{
	int i = 0, num = 0;
	Url **urls;
	urls = (Url **) calloc(count, sizeof(Url *));
	for (i = 0; i < count; ++i) {
		if (links[i] == NULL)
			continue;
		char *host = (char *)calloc(MAX_LINK_LEN, 1);
		char *res = (char *)calloc(MAX_LINK_LEN, 1);
		char *iipp = (char *)calloc(20, 1);
		pretreatLink(links[i]);//目前的作用是判断url是否过长，过长则不爬取
		if (links[i] == NULL)
			continue;
		getHRfromlink(links[i], host, res);
        int j, flag = 0;
        for(j = 0; j < strlen(res); j++){//排除url中的参数
            if(res[j] == '?'){
                flag = 1;
                break;
            }
        }
        if(flag)
            continue;
		//printf("h=%s\tr=%s\n",h,r);
		if (bloomDomain(host)) {	/*domain出现过 */
			if (bloomPath(res)) {	/*path出现过，则忽略此link */
				free(host);
                host = NULL;
				free(res);
                res = NULL;
				free(iipp);
                iipp = NULL;
				continue;
			} else {	/*path没有出现 */
				Url *tmp = (Url *) calloc(1, sizeof(Url));
				tmp->domain = host;
				tmp->path = res;

				map < string, string >::const_iterator itr;
				itr = host_ip_map.find(host);	/*查找domain在map是否有记录 */
				if (itr == host_ip_map.end()) {	/*domain不在map中 */
					tmp->ip = iipp;
					urls[num++] = tmp;	/*放入urls，等待批量DNS解析 */
					//printf("|%s||%s||%s|被放入DNS解析队列\n",urls[num-1]->domain,urls[num-1]->ip,urls[num-1]->path);
				} else {	/*domain在map中 */
					tmp->ip = strdup(host_ip_map[host].c_str());	/*直接从map取出与domain对应的ip */
					//printf("从map中直接取出ip后：%s\t%s\t%s\n",tmp->domain,tmp->ip,tmp->path);
					addurl2queue(tmp);	/*把url放入待爬取队列 */
					continue;
				}
			}
		} else {	/*domain没有出现过 */
			Url *tmp = (Url *) calloc(1, sizeof(Url));
			tmp->domain = host;
			tmp->path = res;
			tmp->ip = iipp;
			urls[num++] = tmp;	/*放入urls，等待批量DNS解析 */
			//printf("|%s||%s||%s|被放入DNS解析队列\n",urls[num-1]->domain,urls[num-1]->ip,urls[num-1]->path);
		}
	}

	dnsParse(urls, num);	/*批量DNS解析 */

	for (i = 0; i < num; ++i)	/*经DNS解析得到ip后，放入url队列 */
		addurl2queue(urls[i]);
	//printf("把links放入url_queue完毕\n");    
	//free(urls);
}

/*把url放入待爬取队列*/
void addurl2queue(Url * url)
{
	if (url == NULL || url->domain == NULL || strlen(url->domain) == 0 || url->path == NULL || strlen(url->path) == 0 || url->ip == NULL || strlen(url->ip) == 0) {
		fprintf(stderr, "Url内容不完整。domain=%s\tpath=%s\tip=%s\n", url->domain, url->path, url->ip);
		return;
	}
	//fprintf(stderr,"Url内容完整。domain=%s\tpath=%s\tip=%s\n",url->domain,url->path,url->ip);
	if (url_queue.size() >= MAX_QUEUE_SIZE)	/*如果队列已满，就忽略譔url */
		return;
	//pthread_mutex_lock(&queue_lock);
	url_queue.push(url);
	//pthread_mutex_unlock(&queue_lock);
	printf("url=%s%s 已放入待爬取队列\n",url->domain,url->path);
}

/*创建分离线程*/
int CreateThread(void *(*start_routine) (void *), void *arg, pthread_t * thread, pthread_attr_t * pAttr)
{
	pthread_attr_t thr_attr;
	if (pAttr == NULL) {
		pAttr = &thr_attr;
		pthread_attr_init(pAttr);	/*初始化线程属性 */
		pthread_attr_setstacksize(pAttr, 1024 * 1024);	/*1 M的堆栈 */
		pthread_attr_setdetachstate(pAttr, PTHREAD_CREATE_DETACHED);	/*线程分离，主线程不需要等子线程结束 */
	}
	pthread_t tid;
	if (thread == NULL) {
		thread = &tid;
	}
	int r = pthread_create(thread, pAttr, start_routine, arg);
	pthread_attr_destroy(pAttr);	/*销毁线程属性 */
	return r;
}
