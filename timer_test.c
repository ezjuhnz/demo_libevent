#include <sys/types.h>
#include <event2/event-config.h>
#include <sys/stat.h>

#include <sys/queue.h>
#include <unistd.h>
#include <time.h>
#ifdef EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>

//gcc timer_test.c -o timer_test -levent -I/usr/local/include -Wl,-rpath=/usr/local/lib
struct timeval lasttime;
int event_is_persistent;

static void timeout_cb(evutil_socket_t fd, short event, void* arg)
{
	printf("timeout_cb begin..\n");
	struct timeval newtime, difference;
	struct event* timeout = arg;
	double elapsed;

	evutil_gettimeofday(&newtime, NULL);
	evutil_timersub(&newtime, &lasttime, &difference);
	elapsed = difference.tv_sec + (difference.tv_usec/1.0e6);
	printf("timeout_cb called at %d: %.3f seconds elapsed\n",
		(int)newtime.tv_sec, elapsed);

	lasttime = newtime;
	if(!event_is_persistent){
		struct timeval tv;
		evutil_timerclear(&tv);
		tv.tv_sec = 2;
		event_add(timeout, &tv);
	}
	printf("timeout_cb end..\n");
}

int main(int argc, char** argv)
{
	struct event timeout;
	struct timeval tv;
	struct event_base* base;
	int flags;

	if(argc == 2 && !strcmp(argv[1],"-p")){
		event_is_persistent = 1;
		flags = EV_PERSIST;
	}else{
		event_is_persistent = 0;
		flags = 0;
	}

	base = event_base_new();
	event_assign(&timeout, base, -1, flags, timeout_cb, (void*)&timeout);
	printf("event_assign end==\n");
	evutil_timerclear(&tv);
	tv.tv_sec = 2;
	event_add(&timeout, &tv);

	evutil_gettimeofday(&lasttime, NULL);
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	printf("event_base_dispatch begin==\n");
	event_base_dispatch(base);
	printf("event_base_dispatch end==\n");
	return 0;
}