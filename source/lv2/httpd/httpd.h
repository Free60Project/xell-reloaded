#ifndef __httpd_h
#define __httpd_h

#define MAX_LINESIZE 1024

#define HTTPD_CLIENT_REQUEST 0
#define HTTPD_CLIENT_HEADER  1
#define HTTPD_CLIENT_DATA    2
#define HTTPD_CLIENT_IDLE    3

#define HTTPD_SERVER_IDLE     0
#define HTTPD_SERVER_RESPONSE 1
#define HTTPD_SERVER_HEADER   2
#define HTTPD_SERVER_DATA     3
#define HTTPD_SERVER_CLOSE    4

#define HTTPD_ERR_KV_READ     0x10

#define SENDBUFFER_LEN 40960

struct http_state;

extern void httpd_start(void);

extern int httpd_available_sendbuffer(struct http_state *http);
extern void httpd_put_sendbuffer(struct http_state *http, const void *data, int len);
extern void httpd_put_sendbuffer_string(struct http_state *http, const char *data);
extern int httpd_do_std_header(struct http_state *http);

struct httpd_handler
{
	int (*process_request)(struct http_state *http, const char *method, const char *url);
	void (*process_header)(struct http_state *http, const char *option, const char *val);
	void (*start_response)(struct http_state *http);
	int (*do_header)(struct http_state *http);
	int (*do_data)(struct http_state *http);
	int (*process_data)(struct http_state *http, const void *data, int len); /* return != len *ONLY* in case you changed the state! */
	void (*finish)(struct http_state *http);
};

extern struct httpd_handler http_handler[];

struct http_state
{
	int state_server, state_client;
	
	int code;
	char *code_descr;
	int code_ex;
	
	char linebuffer[MAX_LINESIZE + 1];
	int linebuffer_ptr;
	
	char sendbuffer[SENDBUFFER_LEN];
	int sendbuffer_read, sendbuffer_write;
	int std_header_state;
	int retries;
	int isserial;
	
	struct httpd_handler *handler;
	void *response_priv;
};


#endif
