#include "util-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <arpa/inet.h>
//running command: "./server-http -p 8000  -m 1024 -H 0.0.0.0 ./"
//gcc server-http.c -o server-http -levent -I ./libevent/include/ -Wl,-rpath=/usr/local/lib
char uri_root[512];

static const struct table_entry{
	const char* extension;
	const char* content_type;
}content_type_table[] = {
	{"tex", "text/plain"},
	{ "c", "text/plain" },
	{ "h", "text/plain" },
	{ "html", "text/html" },
	{ "htm", "text/htm" },
	{ "css", "text/css" },
	{ "gif", "image/gif" },
	{ "jpg", "image/jpeg" },
	{ "jpeg", "image/jpeg" },
	{ "png", "image/png" },
	{ "pdf", "application/pdf" },
	{ "ps", "application/postscript" },
	{ NULL, NULL },
};

struct options{
	int port;
	int iocp;
	int verbose;
	int max_body_size;

	int unlink;
	const char* unixsock;
	const char* bind;
	const char* docroot;
};

//打印"使用帮助"
static void print_usage(FILE* out, const char* porg, int exit_code)
{
	fprintf(out,
		"Syntax: %s [ OPTS ] <docroot>\n"
		" -p      - port\n"
		" -U      - bind to unix socket\n"
		" -H      - address to bind (default: 0.0.0.0)\n"
		" -u      - unlink unix socket before bind\n"
		" -I      - IOCP\n"
		" -m      - max body size\n"
		" -v      - verbosity, enables libevent debug logging too\n", porg);
	exit(exit_code);
}

//解析命令行参数
static struct options parse_opts(int argc, char** argv)
{
	struct options o;
	int opt;
	memset(&o, 0, sizeof(o));
	while((opt = getopt(argc, argv, "hp:U:m:uIvH:")) != -1){
		switch(opt){
			case 'p': o.port = atoi(optarg); break;
			case 'U': o.unixsock = optarg; break;
			case 'u': o.unlink = 1; break;
			case 'I': o.iocp = 1; break;
			case 'm': o.max_body_size = atoi(optarg); break;
			case 'v': ++o.verbose; break;
			case 'H': o.bind = optarg; break;
			case 'h': print_usage(stdout, argv[0], 0); break;
			default: fprintf(stderr, "Unknown option %c\n", opt); break;
		}
	}

	if(optind >= argc || (argc - optind) > 1){
		print_usage(stdout, argv[0], 1);
	}
	o.docroot = argv[optind];

	return o;
}

//打印IP,端口号
static int display_listen_sock(struct evhttp_bound_socket* handle)
{
	struct sockaddr_storage ss;
	evutil_socket_t fd;
	ev_socklen_t socklen = sizeof(ss);
	char addrbuf[128];
	void* inaddr;
	const char* addr;
	int got_port = -1;

	fd = evhttp_bound_socket_get_fd(handle);
	memset(&ss, 0, sizeof(ss));
	if(getsockname(fd, (struct sockaddr*)&ss, &socklen)){
		perror("getsockname");
		return 1;
	}

	if(ss.ss_family == AF_INET){
		got_port = ntohs(((struct sockaddr_in*)&ss)->sin_port);
		inaddr = &((struct sockaddr_in*)&ss)->sin_addr;
	}else if (ss.ss_family == AF_INET6) {
		got_port = ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
		inaddr = &((struct sockaddr_in6*)&ss)->sin6_addr;
	}else{
		fprintf(stderr, "Weird address family %d\n",
		    ss.ss_family);
		return 1;
	}

	addr = evutil_inet_ntop(ss.ss_family, inaddr, addrbuf, 
		sizeof(addrbuf));
	if(addr){
		printf("Listening on %s:%d\n", addr, got_port);
		evutil_snprintf(uri_root, sizeof(uri_root),
		    "http://%s:%d",addr,got_port);
	}else{
		fprintf(stderr, "evutil_inet_ntop failed\n");
		return 1;
	}
	return 0;
}

//处理SIGINT信号
static void do_term(int sig, short events, void* arg){
	struct event_base* base = arg;
	event_base_loopbreak(base);
	fprintf(stderr, "Got %i, Terminating\n", sig);
}

//获取content-type
static const char* guess_content_type(const char* path)
{
	const char* last_period;
	const char* extension;
	const struct table_entry* ent;
	last_period = strrchr(path, '.');
	if(!last_period || strchr(last_period, '/'))
		goto not_found;
	extension = last_period + 1;
	for(ent = &content_type_table[0]; ent->extension; ++ent){
		if(!evutil_ascii_strcasecmp(ent->extension, extension))
			return ent->content_type;
	}

not_found:
	return "application/misc";
}

static void dump_request_cb(struct evhttp_request* req, void* arg)
{
	const char* cmdtype;
	struct evkeyvalq* headers;
	struct evkeyval* header;
	struct evbuffer* buf;

	switch(evhttp_request_get_command(req)){
		case EVHTTP_REQ_GET: cmdtype = "GET"; break;
		case EVHTTP_REQ_POST: cmdtype = "POST"; break;
		case EVHTTP_REQ_HEAD: cmdtype = "HEAD"; break;
		case EVHTTP_REQ_PUT: cmdtype = "PUT"; break;
		case EVHTTP_REQ_DELETE: cmdtype = "DELETE"; break;
		case EVHTTP_REQ_OPTIONS: cmdtype = "OPTIONS"; break;
		case EVHTTP_REQ_TRACE: cmdtype = "TRACE"; break;
		case EVHTTP_REQ_CONNECT: cmdtype = "CONNECT"; break;
		case EVHTTP_REQ_PATCH: cmdtype = "PATCH"; break;
		default: cmdtype = "unknown"; break;
	}
	printf("Received a %s request for %s\nHeaders:\n",
    cmdtype, evhttp_request_get_uri(req));

    headers = evhttp_request_get_input_headers(req);
    for(header = headers->tqh_first; header;
    	header = header->next.tqe_next){
    	printf(" %s: %s\n", header->key, header->value);
    }
    buf = evhttp_request_get_input_buffer(req);
    puts("Input data:<<<");
    while(evbuffer_get_length(buf)){
    	int n;
    	char cbuf[128];
    	n = evbuffer_remove(buf, cbuf, sizeof(buf));
    	if(n > 0)
    		fwrite(cbuf, 1, n, stdout);
    }
    puts(">>>");
    evhttp_send_reply(req, 200, "OK", NULL);
}

static void send_document_cb(struct evhttp_request* req, void* arg)
{
	struct evbuffer* evb = NULL;
	struct options* o = arg;
	const char* uri = evhttp_request_get_uri(req);
	struct evhttp_uri* decoded = NULL;
	const char* path;
	char* decoded_path;
	char* whole_path = NULL;
	size_t len;
	int fd = -1;
	struct stat st;
	//获取请求方式,不是GET方法则调用dump_request_cb函数
	if(evhttp_request_get_command(req) != EVHTTP_REQ_GET){
		dump_request_cb(req, arg);
		return;
	}
	printf("Got a GET request for <%s>\n",  uri);

	//Decode uri
	decoded = evhttp_uri_parse(uri);
	if(!decoded){
		printf("It's not a good URI.\n");
		evhttp_send_error(req, HTTP_BADREQUEST, 0);
		return;
	}
	//从uri中获取path
	path = evhttp_uri_get_path(decoded);
	if(!path) path = "/";

	//decode path
	decoded_path = evhttp_uridecode(path, 0, NULL);
	if(decoded_path == NULL){
		goto err;
	}
	//禁止在path中使用".."
	if(strstr(decoded_path, "..")){
		goto err;
	}
	len = strlen(decoded_path) + strlen(o->docroot) + 2;
	if(!(whole_path = malloc(len))){
		perror("malloc");
		goto err;
	}
	//拼凑文件路径whole_path
	evutil_snprintf(whole_path, len, "%s/%s", o->docroot, decoded_path);
	//检测文件是否存在
	if(stat(whole_path, &st) < 0){
		goto err;
	}
	evb = evbuffer_new();

	if(S_ISDIR(st.st_mode)){
		DIR* d;
		struct dirent* ent;
		const char* trailing_slash = "";
		if(!strlen(path) || path[strlen(path)-1] != '/'){
			trailing_slash = "/";
		}
		if(!(d = opendir(whole_path))){
			goto err;
		}

		evbuffer_add_printf(evb,
                    "<!DOCTYPE html>\n"
                    "<html>\n <head>\n"
                    "  <meta charset='utf-8'>\n"
		    "  <title>%s</title>\n"
		    "  <base href='%s%s'>\n"
		    " </head>\n"
		    " <body>\n"
		    "  <h1>%s</h1>\n"
		    "  <ul>\n",
		    decoded_path, /* XXX html-escape this. */
		    path, /* XXX html-escape this? */
		    trailing_slash,
		    decoded_path /* XXX html-escape this */);

		while((ent = readdir(d))){
			const char* name = ent->d_name;
			evbuffer_add_printf(evb,
			    "    <li><a href=\"%s\">%s</a>\n",
			    name, name);/* XXX escape this */
		}
		evbuffer_add_printf(evb, "</ul></body></html>\n");
		closedir(d);
		evhttp_add_header(evhttp_request_get_output_headers(req),
			"Content_Type", "text/html");
	}else{ //NOT dir
		const char* type = guess_content_type(decoded_path);
		if((fd = open(whole_path, O_RDONLY)) < 0){
			perror("open");
			goto err;
		}

		if(fstat(fd, &st) < 0){
			perror("fstat");
			goto err;
		}
		evhttp_add_header(evhttp_request_get_output_headers(req),
			"Content-Type", type);
		evbuffer_add_file(evb, fd, 0, st.st_size);
	}

	evhttp_send_reply(req, 200, "OK", evb);
	goto done;
err:
	evhttp_send_error(req, HTTP_NOTFOUND, NULL);
	if(fd >= 0) close(fd);

done:
	if(decoded) evhttp_uri_free(decoded);
	if(decoded_path) free(decoded_path);
	if(whole_path) free(whole_path);
	if(evb) evbuffer_free(evb);
}	

int main(int argc, char** argv)
{
	struct event_config* cfg = NULL;
	struct event_base* base = NULL;
	struct evhttp* http = NULL;
	struct evhttp_bound_socket* handle = NULL;
	struct evconnlistener* lev = NULL;
	struct event* term = NULL;
	struct options o = parse_opts(argc, argv);
	int ret = 0;

	//向关闭的socket中写数据会导致SIGPIPE信号发出,此处我们忽略SIGPIPE信号
	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR){
		ret = 1;
		goto err;
	}

	//标准输出,标准错误不使用缓冲,这可以使数据尽快展示在终端
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	cfg = event_config_new();

	base = event_base_new_with_config(cfg);
	if(!base){
		perror("event_base_new_with_config");
		ret = 1;
	}
	event_config_free(cfg);
	cfg = NULL;

	//创建http服务器
	http = evhttp_new(base);
	if(!http){
		perror("evhttp_new");
		ret = 1;
	}
	//给http服务器设置回调函数
	evhttp_set_cb(http, "/dump", dump_request_cb, NULL);

	evhttp_set_gencb(http, send_document_cb, &o);
	if(o.max_body_size){
		evhttp_set_max_body_size(http, o.max_body_size);
	}

	handle = evhttp_bind_socket_with_handle(http, o.bind, o.port);
	if(!handle){
		perror("evhttp_bind_socket_with_handle");
		ret = 1;
		goto err;
	}
	//打印socket绑定的IP和端口
	if(display_listen_sock(handle)){
		ret = 1;
		goto err;
	}
	//注册信号事件
	term = evsignal_new(base, SIGINT, do_term, base);
	if(!term){
		goto err;
	}
	if(event_add(term, NULL)){
		goto err;
	}
	event_base_dispatch(base);

	err:
	if (cfg)
		event_config_free(cfg);
	if (http)
		evhttp_free(http);
	if (term)
		event_free(term);
	if (base)
		event_base_free(base);

	return ret;
}