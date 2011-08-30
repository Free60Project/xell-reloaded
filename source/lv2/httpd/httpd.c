/* pure event driven httpd server */
#ifdef UNIX
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#define mem_free free
#define mem_malloc malloc
#else
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#endif

#include "httpd/httpd.h"

#include "network/network.h"
#include "httpd/httpd_flash.h"

#include "httpd_keyvault.h"
#include "httpd_keyvault2.h"

#ifndef HTTPD_VFS		
#include "vfs.h"
#include "httpd_index.h"
#endif

#define hdprintf(x...)
// fprintf(stderr, x)

/* do_* 0: "i'm done, go to next state", 1: "i'm waiting for more sendbuffer space, poll me again", 2: "poll me again NOW!" */

static void httpd_handle_server(struct http_state *http);

extern unsigned long simple_strtoul(const char *cp,char **endp,unsigned int base);

struct tcp_pcb *listen_pcb;

void httpd_init(struct http_state *http)
{
	http->state_server = HTTPD_SERVER_IDLE;
	http->state_client = HTTPD_CLIENT_REQUEST;
	http->code_descr = 0;
	http->code = 500;
	http->linebuffer_ptr = 0;
	http->std_header_state = 0;
	http->sendbuffer_read = http->sendbuffer_write = 0;
	http->handler = 0;
}

#ifdef UNIX
void httpd_try_flush_sendbuffer(struct http_state *http)
{
	int a = http->sendbuffer_write - http->sendbuffer_read;
	if (a > 0)
	{
		write(1, http->sendbuffer + http->sendbuffer_read, a);
	} else if (a < 0)
	{
		write(1, http->sendbuffer + http->sendbuffer_read, SENDBUFFER_LEN - http->sendbuffer_read);
		write(1, http->sendbuffer, http->sendbuffer_write);
	}
	
	http->sendbuffer_read = http->sendbuffer_write;
}
#else

static int tcp_do_send(struct tcp_pcb *pcb, void *data, int len)
{
	err_t err;
	do {
		err = tcp_write(pcb, data, len, 1);
		if (err == ERR_MEM)
			len /= 2;
	} while (err == ERR_MEM && len > 1);
	
	if (err == ERR_OK) {
		return len;
	} else 
		printf("send_data: error %d len %d %d\n", err, len, tcp_sndbuf(pcb));
	return 0;
}

static void
send_data(struct tcp_pcb *pcb, struct http_state *http)
{
	httpd_handle_server(http);
	int av = tcp_sndbuf(pcb), a = http->sendbuffer_write - http->sendbuffer_read;
	
	if (a < 0)
		a = SENDBUFFER_LEN - http->sendbuffer_read;

	if (av < a)
		a = av;

	http->sendbuffer_read += tcp_do_send(pcb, http->sendbuffer + http->sendbuffer_read, a);

	if (http->sendbuffer_read == SENDBUFFER_LEN)
		http->sendbuffer_read = 0;
}
#endif

int httpd_available_sendbuffer(struct http_state *http)
{
	int available = http->sendbuffer_read - http->sendbuffer_write;
	if (available <= 0)
		available += SENDBUFFER_LEN;
	if (available < 10)
		return 0;
	hdprintf("%d bytes avail\n", available - 10);
	return available - 10;
}

void httpd_put_sendbuffer(struct http_state *http, const void *data, int len)
{
	int available = SENDBUFFER_LEN - http->sendbuffer_write;
	if (available >= len)
	{
		memcpy(http->sendbuffer + http->sendbuffer_write, data, len);
		http->sendbuffer_write += len;
	} else
	{
		memcpy(http->sendbuffer + http->sendbuffer_write, data, available);
		len -= available;
		data = ((char*)data) + available;
		memcpy(http->sendbuffer, data, len);
		http->sendbuffer_write = len;
	}
	if (http->sendbuffer_write == SENDBUFFER_LEN)
		http->sendbuffer_write = 0;
}

void httpd_put_sendbuffer_string(struct http_state *http, const char *data)
{
	httpd_put_sendbuffer(http, data, strlen(data));
}

void httpd_get_descr(struct http_state *http)
{
	switch (http->code)
	{
	case 200:
		http->code_descr = "OK";
		break;
	case 404:
		http->code_descr = "Not Found";
		break;
	case 400:
		http->code_descr = "Bad Request";
		break;
	default:
	case 500:
		http->code_descr = "Internal Server Error";
		break;
	}
}

static inline char *httpd_receive_char(struct http_state *http, char c)
{
	if (c == '\r')
		return 0;
	if (http->linebuffer_ptr < MAX_LINESIZE)
		http->linebuffer[http->linebuffer_ptr++] = c;
	if (c == '\n')
	{
		http->linebuffer[http->linebuffer_ptr++] = 0;
		http->linebuffer_ptr = 0;
		return http->linebuffer;
	}
	return 0;
}

static void httpd_find_handler(struct http_state *http, const char *method, const char *path)
{
	struct httpd_handler *h = http_handler;
	while (!h->process_request(http, method, path))
		++h;
	http->handler = h;
}

static inline void httpd_receive_request(struct http_state *http, char c)
{
	char *line = httpd_receive_char(http, c);
	if (!line)
		return;
	
	char *method = line;
	char *path = line;
	while (*path)
	{
		if (*path == ' ')
		{
			*path++=0;
			break;
		}
		++path;
	}
	
	char *version = path;
	while (*version)
	{
		if (*version == ' ')
		{
			*version++=0;
			break;
		}
		++version;
	}
	
	if ((!*path) || (!*version) || strncmp(version, "HTTP/1.", 7))
	{
		http->code = 400;
		path = "";
	}
	
	hdprintf("received client Request: method=%s path=%s\n", method, path);
	
	httpd_find_handler(http, method, path);
	
	http->state_client = HTTPD_CLIENT_HEADER;
}

void httpd_start_response(struct http_state *http)
{
		/* client header received */
	http->state_client = HTTPD_CLIENT_DATA; // or idle
	http->state_server = HTTPD_SERVER_RESPONSE;
	
	if (http->handler->start_response)
		http->handler->start_response(http);
}

static inline void httpd_receive_header(struct http_state *http, char c)
{
	char *line = httpd_receive_char(http, c);
	if (!line)
		return;
	
		/* empty line? */
	if (line[0] == '\n')
	{
		httpd_start_response(http);
		return;
	}
	
	char *val = line;
	while (*val)
	{
		if (*val == ':')
		{
			*val++=0;
			if (*val == ' ')
				val++;
			break;
		}
		++val;
	}
	
	if (val[strlen(val)-1]=='\n')
		val[strlen(val)-1]=0;

	hdprintf("received client header %s:%s\n", line, val);
	if (http->handler->process_header)
		http->handler->process_header(http, line, val);
}

static inline int httpd_receive_data(struct http_state *http, const void *data, int len)
{
	if (http->handler->process_data)
		return http->handler->process_data(http, data, len);
	else
		return len;
}

int httpd_do_std_header(struct http_state *http)
{
	const char *s=0;
	switch (http->std_header_state)
	{
	case 0:
		s = "Server: yahd v1.0\r\n";
		break;
	case 1:
		s = "\r\n";
		break;
	case 2:
		s = 0;
		break;
	}
	
	if (s)
	{
		int av = httpd_available_sendbuffer(http);
		if (av < strlen(s))
			return 1;
		
		httpd_put_sendbuffer_string(http, s);
		++http->std_header_state;
		return 2;
	} else
		return 0;
}

static void httpd_handle_server(struct http_state *http)
{
	int done = 0, busy = 0;
	while (!done)
	{
		switch (http->state_server)
		{
		case HTTPD_SERVER_IDLE:
			done = 1;
			break;
		case HTTPD_SERVER_RESPONSE:
		{
			hdprintf("send response\n");
			if (!http->code_descr)
				httpd_get_descr(http);
			int reslen = 13 + strlen(http->code_descr);
				// HTTP/1.0 200 OK  -> 8 + 1 + 3 + 1 + strlen(code_descr);
			if (httpd_available_sendbuffer(http) < reslen)
			{
				busy = 1;
				break;
			}
			char code[5];
			code[0] = (http->code / 100) + '0';
			code[1] = (http->code / 10) % 10 + '0';
			code[2] = http->code % 10 + '0';
			code[3] = ' ';
			code[4] = 0;
			httpd_put_sendbuffer_string(http, "HTTP/1.0 ");
			httpd_put_sendbuffer_string(http, code);
			httpd_put_sendbuffer_string(http, http->code_descr);
			httpd_put_sendbuffer_string(http, "\r\n");
			http->state_server = HTTPD_SERVER_HEADER;
			hdprintf("now in header state!\n");
			break;
		}
		case HTTPD_SERVER_HEADER:
		{
			int r = 1;
			if (http->handler->do_header)
				r = http->handler->do_header(http);
			else
				r = httpd_do_std_header(http);
			if (r == 0)
				http->state_server = HTTPD_SERVER_DATA;
			else if (r == 1)
				busy = 1;
			break;
		}
		case HTTPD_SERVER_DATA:
		{
			int r = 1;
			if (http->handler->do_data)
				r = http->handler->do_data(http);
			if (r == 0)
				http->state_server = HTTPD_SERVER_CLOSE;
			else if (r == 1)
				busy = 1;
			break;
		}
		case HTTPD_SERVER_CLOSE:
			if (http->handler)
			{
				if (http->handler->finish)
					http->handler->finish(http);
			}
			http->handler = 0;
			done = 1;
			break;
		}

#ifdef UNIX		
		if (http->sendbuffer_read != http->sendbuffer_write)
			httpd_try_flush_sendbuffer(http);
#else
		if (busy)
			return;
#endif
	}
}

void httpd_receive(struct http_state *http, const char *data, int len)
{
	while (len)
	{
		switch (http->state_client)
		{
		case HTTPD_CLIENT_REQUEST:
			httpd_receive_request(http, *data++);
			len--;
			break;
		case HTTPD_CLIENT_HEADER:
			httpd_receive_header(http, *data++);
			len--;
			break;
		case HTTPD_CLIENT_DATA:
		{
			int r = httpd_receive_data(http, data, len);
			data += r;
			len -= r;
			break;
		}
		default:
			data += len;
			len = 0;
		}
	}
	
	httpd_handle_server(http);
}

/* ---------- response handlers */

struct response_static_priv_s
{
	const char *data;
	int ptr, len;
};

static void response_static_process_request(struct http_state *http, const char *text)
{
	http->response_priv = mem_malloc(sizeof(struct response_static_priv_s));
	if (!http->response_priv)
		return;
	struct response_static_priv_s *priv = http->response_priv;
	priv->data = text;
	priv->ptr = 0;
	priv->len = strlen(priv->data);
}

static int response_static_do_data(struct http_state *http)
{
	struct response_static_priv_s *priv = http->response_priv;
	if (!priv)
		return 0;
	
	int av = httpd_available_sendbuffer(http);
	
	hdprintf("err_do_data, %d %d\n", av, priv->len);

	if (!av)
		return 1;

	if (av > priv->len)
		av = priv->len;
	
	httpd_put_sendbuffer(http, priv->data + priv->ptr, av);
	priv->ptr += av;
	priv->len -= av;
	if (!priv->len)
		return 0;
	else
		return 1;
}

static void response_static_finish(struct http_state *http)
{
	mem_free(http->response_priv);
}

#ifdef UNIX
 	/* ---------- fs handler */
struct response_file_priv_s
{
	FILE *f;
};

static int response_file_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;
	FILE *f = fopen(url + 1, "r");
	if (!f)
		return 0;
	http->response_priv = mem_malloc(sizeof(struct response_file_priv_s));
	if (!http->response_priv)
	{
		fclose(f);
		return 0;
	}
	struct response_file_priv_s *priv = http->response_priv;
	priv->f = f;
	http->code = 200;
	return 1;
}

static int response_file_do_data(struct http_state *http)
{
	struct response_file_priv_s *priv = http->response_priv;
	
	int av = httpd_available_sendbuffer(http);
	
	if (!av)
		return 1;

	unsigned char buff[av];
	av = fread(buff, 1, av, priv->f);
	if (av <= 0)
		return 0;
	
	httpd_put_sendbuffer(http, buff, av);

	return 1;
}

static void response_file_finish(struct http_state *http)
{
	struct response_file_priv_s *priv = http->response_priv;
	fclose(priv->f);
	mem_free(priv);
}
#endif

#ifdef HTTPD_VFS		
 	/* ---------- vfs handler */

struct response_vfs_priv_s
{
	struct vfs_entry_s *f;
	int ptr, hdr_state;
};

static int response_vfs_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;
	
	if (!strcmp(url, "/"))
		url = "/index.html";
	
	struct vfs_entry_s *f = search_file(url);
	if (!f)
		return 0;
	
	http->response_priv = mem_malloc(sizeof(struct response_vfs_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_vfs_priv_s *priv = http->response_priv;
	priv->f = f;
	priv->hdr_state = 0;
	priv->ptr = 0;
	http->code = 200;
	return 1;
}

static int response_vfs_do_dynamic(struct http_state *http, const char *cmd, int len)
{
	int av = httpd_available_sendbuffer(http);
	char *res;
	len++;
	if (len == 1)
		res = "%";
	else
		res = "illegal dhtml primitive";
	if (av < strlen(res))
		return 1;
	httpd_put_sendbuffer_string(http, res);
	return 0;
}

static int response_vfs_do_header(struct http_state *http)
{
	struct response_vfs_priv_s *priv = http->response_priv;
	
	const char *t=0, *o=0;
	char buf[32];
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		o = priv->f->mime_type;
		break;
	case 1:
		if (!(priv->f->flags & 1))
		{
			t = "Content-Length";
			sprintf(buf, "%d", priv->f->len);
			o = buf;
			break;
		}
	case 2:
		return httpd_do_std_header(http);
	}
	
	int av = httpd_available_sendbuffer(http);
	if (av < (strlen(t) + strlen(o) + 4))
		return 1;
	
	httpd_put_sendbuffer_string(http, t);
	httpd_put_sendbuffer_string(http, ": ");
	httpd_put_sendbuffer_string(http, o);
	httpd_put_sendbuffer_string(http, "\r\n");
	++priv->hdr_state;
	return 2;
}

static int response_vfs_do_data(struct http_state *http)
{
	struct response_vfs_priv_s *priv = http->response_priv;
	
	int av = httpd_available_sendbuffer(http);
	
	if (!av)
		return 1;

			/* static content is easy... */
	if (!(priv->f->flags & 1))
	{
		if (av > (priv->f->len - priv->ptr))
			av = priv->f->len - priv->ptr;
	
		if (!av)
			return 0;

		httpd_put_sendbuffer(http, priv->f->data + priv->ptr, av);
		priv->ptr += av;
	} else
	{
		const char *b = priv->f->data + priv->ptr, *e = b;
		while ((e - b) < av)
		{
			hdprintf("%d bytes parsed, %d av\n", e- b, av);
			if (e == priv->f->data + priv->f->len)
				break;
			if (*e == '%')
			{
				if (e != b)
				{
					httpd_put_sendbuffer(http, b, e - b);
					priv->ptr = e - priv->f->data;
					av -= e - b;
				}
				
				b = ++e;
				
						/* length check missing here: wer mit dhtml dingern rummesst hat selbst schuld! */
				while (*e != '%')
					++e;
				
				int l = e - b;
				int r = response_vfs_do_dynamic(http, b, l);
				if (r == 1) // "out of sendbuf space"
				{
					hdprintf("out of sendbuf space\n");
					return 1;
				}
				
				av = httpd_available_sendbuffer(http);
				b = ++e;
				priv->ptr = e - priv->f->data;
			} else
				++e;
		}
		if (e != b)
		{
			httpd_put_sendbuffer(http, b, e - b);
			priv->ptr = e - priv->f->data;
		}
		
		if (e == priv->f->data + priv->f->len)
			return 0;
	}

	return 1;
}

static void response_vfs_finish(struct http_state *http)
{
	struct response_vfs_priv_s *priv = http->response_priv;
	mem_free(priv);
}
#endif

struct response_mem_priv_s
{
	void *base;
	int len;
	int ptr, hdr_state;
};

static int response_mem_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;
		
	if (strcmp(url, "/MEM"))
		return 0;

	http->response_priv = mem_malloc(sizeof(struct response_mem_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_mem_priv_s *priv = http->response_priv;

//	priv->base = (void*) 0x80000200c8000000ULL;
//	priv->base = (void*) 0x80000200c8000000ULL;
//	priv->base = (void*) 0x8000020000000000ULL;
	priv->base = (void*) 0x80000000;
	priv->hdr_state = 0;
	priv->ptr = 0;
	priv->len = 512 * 1024 * 1024;
//	priv->len = 128 * 1024;
	http->code = 200;
	return 1;
}

static int response_mem_do_header(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;
	
	const char *t=0, *o=0;
	char buf[32];
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		o = "application/binary";
		break;
	case 1:
		t = "Content-Length";
		sprintf(buf, "%d", priv->len);
		o = buf;
		break;
	case 2:
		return httpd_do_std_header(http);
	}
	
	int av = httpd_available_sendbuffer(http);
	if (av < (strlen(t) + strlen(o) + 4))
		return 1;
	
	httpd_put_sendbuffer_string(http, t);
	httpd_put_sendbuffer_string(http, ": ");
	httpd_put_sendbuffer_string(http, o);
	httpd_put_sendbuffer_string(http, "\r\n");
	++priv->hdr_state;
	return 2;
}

static int response_mem_do_data(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;
	
	int av = httpd_available_sendbuffer(http);
	
	if (!av)
	{
		printf("no httpd sendbuffer space\n");
		return 1;
	}
	
	if (av > (priv->len - priv->ptr))
		av = priv->len - priv->ptr;
	
	while (av)
	{
		int maxread = 1024;
		if (maxread > av)
			maxread = av;
		httpd_put_sendbuffer(http, (void*)(priv->base + priv->ptr), maxread);
		priv->ptr += maxread;
		av -= maxread;
	}

	return 1;
}

static void response_mem_finish(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;
	mem_free(priv);
}

static int response_fuses_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;
		
	if (strcmp(url, "/FUSE"))
		return 0;

	extern char FUSES[350]; /* this has to be static */
	http->code = 200;
	response_static_process_request(http, FUSES);
	return 1;
}


 	/* ---------- err400 handler */

static int response_err400_process_request(struct http_state *http, const char *method, const char *url)
{
	if (http->code != 400)
		return 0;
	response_static_process_request(http, "<html>\n<head>\n<title>XeLL - 400</title>\n</head>\n<body>\n<h1>400 - BAD REQUEST</h1>\n</body>\n</html>\n");
	return 1;
}

 	/* ---------- err404 handler */

static int response_err404_process_request(struct http_state *http, const char *method, const char *url)
{
	http->code = 404;
	response_static_process_request(http, "<html>\n<head>\n<title>XeLL - 404</title>\n</head>\n<body>\n<h1>404 - Document not found</h1>\n</body>\n</html>\n");
	return 1;
}

struct httpd_handler http_handler[]=
	{
#ifdef UNIX
		{response_file_process_request, 0, 0, 0, response_file_do_data, 0, response_file_finish},
#endif
#ifndef UNIX
		{response_mem_process_request, 0, 0, response_mem_do_header, response_mem_do_data, 0, response_mem_finish},
#endif
		{response_fuses_process_request, 0, 0, 0, response_static_do_data, 0, response_static_finish},
#ifdef HTTPD_VFS		
		{response_vfs_process_request, 0, 0, response_vfs_do_header, response_vfs_do_data, 0, response_vfs_finish},
#else
		{response_index_process_request, 0, 0, response_index_do_header, response_index_do_data, 0, response_index_finish},
#endif
		{response_flash_process_request,  0, 0, response_flash_do_header, response_flash_do_data, response_flash_finish},
		{response_keyvault_process_request, 0, 0, response_keyvault_do_header, response_keyvault_do_data, 0, response_keyvault_finish},
		{response_keyvault2_process_request, 0, 0, response_keyvault2_do_header, response_keyvault2_do_data, 0, response_keyvault2_finish},
//		{response_setdvdkey_process_request, 0, 0, response_setdvdkey_do_header, response_setdvdkey_do_data, response_setdvdkey_finish},
		{response_err400_process_request, 0, 0, 0, response_static_do_data, 0, response_static_finish},
		{response_err404_process_request, 0, 0, 0, response_static_do_data, 0, response_static_finish}
	};

#ifdef UNIX
int main(void)
{
	struct http_state http;
	
	httpd_init(&http, 0);
	while (1)
	{
		char line[1024];
		int r = read(0, line, 1024);
		if (r < 0)
			break;
		httpd_receive(&http, line, r);
		if (http.state_server == HTTPD_SERVER_CLOSE)
			break;
	}
	return 0;
}
#else

static void
conn_err(void *arg, err_t err)
{
	struct http_state *http;

	http = arg;
	mem_free(http);
}

static void
close_conn(struct tcp_pcb *pcb, struct http_state *http)
{
	if (http->handler && http->handler->finish)
		http->handler->finish(http);
	tcp_arg(pcb, NULL);
	tcp_sent(pcb, NULL);
	tcp_recv(pcb, NULL);
	mem_free(http);
	tcp_close(pcb);
}


static err_t
http_poll(void *arg, struct tcp_pcb *pcb)
{
	struct http_state *http;
	
	http = arg;
	
	if (http == NULL) {
		printf("Null, close\n");
		tcp_abort(pcb);
		return ERR_ABRT;
	} else {
		if (http->handler && (http->sendbuffer_read != http->sendbuffer_write))// (http->state_server != HTTPD_SERVER_IDLE))
		{
			++http->retries;
			if (http->retries == 4) {
				tcp_abort(pcb);
				return ERR_ABRT;
			}
			send_data(pcb, http);
		}
	}

	return ERR_OK;
}

static err_t
http_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	struct http_state *http;

	http = arg;

	http->retries = 0;
	
	if ((http->sendbuffer_read != http->sendbuffer_write) || (http->state_server != HTTPD_SERVER_CLOSE))
		send_data(pcb, http);
	else if (http->state_server == HTTPD_SERVER_CLOSE)
	{
		//printf("closing connection.\n");
		close_conn(pcb, http);
	}

	return ERR_OK;
}

static err_t
http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct http_state *http;
	
	http = arg;

	if (err == ERR_OK && p != NULL) {

		struct pbuf *q;
		for (q = p; q; q = q->next)
			httpd_receive(http, q->payload, q->len);
		pbuf_free(p);

		/* Inform TCP that we have taken the data. */
		tcp_recved(pcb, p->tot_len);

		send_data(pcb, http);
	}

	if (err == ERR_OK && p == NULL) {
		//printf("received NULL\n");
		close_conn(pcb, http);
	}
	return ERR_OK;
}

static err_t
http_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct http_state *http;

	tcp_setprio(pcb, TCP_PRIO_MIN);
	
	/* Allocate memory for the structure that holds the state of the
		 connection. */
	http = mem_malloc(sizeof(struct http_state));

	if (http == NULL) {
		printf("http_accept: Out of memory\n");
		return ERR_MEM;
	}
	
	/* Initialize the structure. */
	http->retries = 0;
	httpd_init(http);
	
	/* Tell TCP that this is the structure we wish to be passed for our
		 callbacks. */
	tcp_arg(pcb, http);

	/* Tell TCP that we wish to be informed of incoming data by a call
		 to the http_recv() function. */
	tcp_recv(pcb, http_recv);

	tcp_err(pcb, conn_err);
	tcp_sent(pcb, http_sent);
	
	tcp_poll(pcb, http_poll, 4);
	
	tcp_accepted(listen_pcb); //lwip 1.3.0

//	printf("accept!\n");
	return ERR_OK;
}

void
httpd_start(void)
{
	//struct tcp_pcb *pcb;

	listen_pcb = tcp_new();
	tcp_bind(listen_pcb, IP_ADDR_ANY, 80);
	listen_pcb = tcp_listen(listen_pcb);
	tcp_accept(listen_pcb, http_accept);
}

#endif
