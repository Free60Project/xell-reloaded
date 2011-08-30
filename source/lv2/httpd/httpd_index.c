/*
 * httpd_index2.c
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

//#include "lwip/debug.h"
//#include "lwip/stats.h"
#include <stdio.h>
#include <string.h>

#include <lwip/tcp.h>
#include <xenon_soc/xenon_io.h>

#include "network/network.h"
#include "xb360/xb360.h"

#include "httpd.h"
#include "httpd_index.h"

unsigned char cpukey[0x10];
unsigned char dvdkey[0x10];

struct response_mem_priv_s
{
	void *base;
	int len;
	int ptr, hdr_state;
	int togo;
	char *filename;
};

int response_index_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;

	if (strcmp(url, "/index.html") !=0  && strcmp(url, "/default.html")  !=0  &&
		strcmp(url, "/index.htm")  !=0  && strcmp(url, "/default.htm")	 !=0  &&
		strcmp(url, "/") !=0)
		return 0;

	http->response_priv = mem_malloc(sizeof(struct response_mem_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_mem_priv_s *priv = http->response_priv;

	memset(cpukey,0x00,0x10);
	cpu_get_key(cpukey);

	memset(dvdkey,0x00,0x10);
	kv_get_dvd_key(dvdkey);


	priv->hdr_state = 0;
	priv->ptr = 0;
	http->code = 200;

	return 1;
}

int response_index_do_header(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;

	const char *t=0, *o=0;
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		//o = "text/html; charset=ISO-8859-1";
		o = "text/html; charset=US-ASCII";
		break;
	case 1:
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

#include "httpd_html.h"

int response_index_do_data(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;

	int i = 0;
	int c = 0;
	int av = httpd_available_sendbuffer(http);

	if (!av)
	{
		printf("no httpd sendbuffer space\n");
		return 1;
	}

	char buffer[1024];

	c = sizeof(INDEX_HTML) / sizeof(*INDEX_HTML);

	for (i=priv->ptr; i<c; ++i)
	{
		memset(buffer, '\0', sizeof(buffer));

		if (strcmp((char *) INDEX_HTML[i], "CPU_KEY")==0){
			sprintf(buffer, "%016llX%016llX",ld(&cpukey[0x0]),ld(&cpukey[0x8]));
		}
		else if(strcmp((char *) INDEX_HTML[i], "DVD_KEY")==0){
			sprintf(buffer, "%016llX%016llX",ld(&dvdkey[0x0]),ld(&dvdkey[0x8]));
		}
		else{
			sprintf(buffer,"%s",INDEX_HTML[i]);
		}

		av -= (int) strlen(buffer);
		if (av<=0) return 1;

		httpd_put_sendbuffer(http, (void*) buffer,(int) strlen(buffer));

		priv->ptr++; // not a pointer, more of an index
	}
	return 0;
}


void response_index_finish(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;
	mem_free(priv);
}
