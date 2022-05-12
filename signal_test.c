#include <sys/types.h>
#include <event2/event-config.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/queue.h>
#include <unistd.h>
#include <sys/time.h>
#else
#include <winsock2.h>
#include <windows.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <event.h>

#ifdef _EVENT___func__
#define __func__ _EVENT___func__
#endif

//gcc signal_test.c -o signal_test -levent -I/usr/local/include -Wl,-rpath=/usr/local/lib
int called = 0;

static void signal_cb(evutil_socket_t fd, short event, void* arg)
{
	struct event* signal = arg;
	printf("signal catched %d\n", EVENT_SIGNAL(signal));
	if(called >= 2){
		event_del(signal);
	}
	called++;
}

int main()
{
	struct event signal_int;
	struct event_base* base;
	int ret;

	base = event_base_new();
	
	event_assign(&signal_int, base, SIGINT, EV_SIGNAL|EV_PERSIST, signal_cb,
             &signal_int);

	//signal_int = evsignal_new(base, SIGINT, signal_cb, NULL);
	
	event_add(&signal_int, NULL);
	printf("1.event_add ==\n");
	event_base_dispatch(base);
	printf("2.event_base_dispatch ==\n");
	event_base_free(base);
	printf("3.event_base_free ==\n");
	return 0;
}