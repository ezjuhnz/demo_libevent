#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#define PORT 9000
//gcc server_toupper.c -o server_toupper -levent -Wl,-rpath=/usr/local/lib/


static void listen_cb(struct evconnlistener*, evutil_socket_t,
	struct sockaddr*, int socklen, void* user_data);
static void read_cb(struct bufferevent*, void* user_data);
static void write_cb(struct bufferevent*, void* user_data);
static void event_cb(struct bufferevent*, short, void* user_data);

int main()
{
	struct event_base* base;
	struct evconnlistener* listener;
	struct sockaddr_in serv_addr;
	//1.实例化event_base
	base = event_base_new();
	if(!base){
		perror("event_base_new");
		return 1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	//2.实例化evconnlistener,监听TCP连接
	listener = evconnlistener_new_bind(base, listen_cb, (void*)base,
		LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr*)&serv_addr, sizeof(serv_addr));
	if(!listener){
		perror("evconnlistener_new_bind");
		return 1;
	}
	//3.循环监听event_base
	event_base_dispatch(base);
	//4.释放listener实例
	evconnlistener_free(listener);
	//5.释放event_base实例
	event_base_free(base);

	printf("done\n");

	return 0;

}

static void listen_cb(struct evconnlistener* listener, evutil_socket_t fd,
	struct sockaddr* sa, int socklen, void* user_data)
{
	printf("listen_cb start...\n");
	struct event_base* base = user_data;
	struct bufferevent* bev;
	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if(!bev){
		perror("bufferevent_socket_new");
		return;
	}
	//设置回调函数
	bufferevent_setcb(bev, read_cb, write_cb, event_cb, "123");
	bufferevent_enable(bev, EV_WRITE);
	bufferevent_enable(bev, EV_READ);
}
static void read_cb(struct bufferevent* bev, void* user_data)
{
	printf("read_cb start...\n");
	printf("user_data=%s\n", (char*)user_data);
	char rdata[1024] = {0};
	int len = sizeof(rdata);
	bufferevent_read(bev, rdata, len);
	printf("read data: %s", rdata);
	int i;
	for(i = 0; i < len; i++){
		rdata[i] = toupper(rdata[i]);
	}
	bufferevent_write(bev, rdata, len);
	printf("read_cb end===\n");
}

static void write_cb(struct bufferevent* bev, void* user_data)
{
	printf("write_cb start...\n");
	struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
		printf("flushed answer\n");
		//bufferevent_free(bev);
	}
}

static void event_cb(struct bufferevent* bev, short events, void* user_data)
{
	printf("write_cb start...\n");
}