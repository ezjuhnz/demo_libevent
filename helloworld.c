#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#define PORT 9000
//gcc helloworld.c -o helloworld -levent -Wl,-rpath=/usr/local/lib/
static const char MESSAGE[] = "Hello, World!\n";

static void listener_cb(struct evconnlistener*, evutil_socket_t,
	struct sockaddr*, int socklen, void*);

static void conn_writecb(struct bufferevent*, void*);
//static void conn_eventcb(struct bufferevent*, short, void*);
//static void signal_cb(evutil_socket_t, short, void*);

int main()
{
	struct event_base* base;
	struct evconnlistener* listener;
	//struct event* signal_event;

	struct sockaddr_in addr = {0};

	base = event_base_new();
	if(!base){
		printf("event_base_new error\n");
		return 1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);

	listener = evconnlistener_new_bind(base, listener_cb, (void*)base,
		LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr*)&addr, sizeof(addr));

	if(!listener){
		printf("evconnlistener_new_bind error\n");
		return 1;
	}

	// signal_event = evsignal_new(base, SIGINT, signal_cb, (void*)base);
	// if(!signal_event || event_add(signal_event, NULL) < 0){
	// 	printf("create or add a signal event error\n");
	// 	return 1;
	// }

	event_base_dispatch(base);

	evconnlistener_free(listener);
	//event_free(signal_event);
	event_base_free(base);

	printf("done\n");
	return 0;
}

static void listener_cb(struct evconnlistener* listener, evutil_socket_t fd,
	struct sockaddr* sa, int socklen, void* user_data)
{
	printf("listener_cb start...\n");
	struct event_base* base = user_data;
	struct bufferevent* bev;
	// Create a new socket bufferevent over an existing socket.
	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if(!bev){
		printf("bufferevent_socket_new error\n");
		event_base_loopbreak(base);
		return;
	}
	//设置read_cb(NULL), write_cb(conn_writecb), error_cb(conn_eventcb)
	bufferevent_setcb(bev, NULL, conn_writecb, NULL, NULL);
	bufferevent_enable(bev, EV_WRITE);
	bufferevent_disable(bev, EV_READ);

	bufferevent_write(bev, MESSAGE, strlen(MESSAGE));
	printf("listener_cb end===\n");
}

static void conn_writecb(struct bufferevent* bev, void* user_data)
{
	printf("conn_writecb start...\n");
	struct evbuffer* output = bufferevent_get_output(bev);
	if(evbuffer_get_length(output) == 0){
		printf("flushed answer\n");
		bufferevent_free(bev);
	}
	printf("conn_writecb end===\n");
}

// static void conn_eventcb(struct bufferevent* bev, short events, void* user_data)
// {
// 	printf("conn_eventcb start...\n");
// 	if(events & BEV_EVENT_EOF){
// 		printf("connection closed\n");
// 	}else if(events & BEV_EVENT_ERROR){
// 		printf("Got an error on the connection\n");
// 	}
// 	bufferevent_free(bev);
// 	printf("conn_eventcb end===\n");
// }

// static void signal_cb(evutil_socket_t sig, short events, void* user_data)
// {
// 	printf("signal_cb start...\n");
// 	struct event_base* base = user_data;
// 	struct timeval delay = {2, 0};
// 	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

// 	event_base_loopexit(base, &delay);
// 	printf("signal_cb end===\n");
// }
