/*
 * httpd_fuse.c
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

//#include "lwip/debug.h"
//#include "lwip/stats.h"
#include "lwip/tcp.h"

#include <string.h>
#include <xetypes.h>

#include "network/network.h"
#include "crypt/hmac_sha1.h"
#include "crypt/rc4.h"
#include "xb360/xb360.h"
#include <xenon_soc/xenon_io.h>

#include "httpd.h"
#include "httpd_fuse.h"


struct response_fuse_priv_s
{
	char *data;
	int len;
	int togo;
	int ptr, hdr_state;
};

#define MAX_FUSE_RESPONSE 2048

char * fuse_strcat(char * dest, const char * src)
{
        char *tmp = dest;

        while (*dest)
                dest++;
        while ((*dest++ = *src++) != '\0')
                ;

        return tmp;
}

int response_fuse_process_request(struct http_state *http, const char *method, const char *url)
{
  int i;
  char temp[150];

	if (strcmp(method, "GET"))
		return 0;

	//printf("url: %s\n",url);

	if (strcmp(url, "/FUSE"))
		return 0;

	http->response_priv = mem_malloc(sizeof(struct response_fuse_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_fuse_priv_s *priv = http->response_priv;

	priv->data = (char*)mem_malloc(MAX_FUSE_RESPONSE);
        
        extern char FUSES[350];
        fuse_strcat(priv->data,FUSES);
/*	sprintf(priv->data, "Fuses dump\r\n");
	for (i=0; i<12; ++i) {
		memset(temp, '\0', sizeof(temp));
                sprintf(temp, "fuseset %02d: %016llX\r\n", i, (unsigned long long)xenon_secotp_read_line(i));
		fuse_strcat(priv->data, temp);
	}
*/      
        unsigned char key[0x10 + 1];
        memset(key,'\0',sizeof(key));
        
	if (cpu_get_key(key)==0)
	{
                memset(temp, '\0', sizeof(temp));
                sprintf(temp, "\r\nYour CPU key : %016llX%016llX\r\n", ld(&key[0x0]),ld(&key[0x8]));
                fuse_strcat(priv->data, temp);
        }
        
	memset(key, '\0', sizeof(key));
	if (kv_get_dvd_key(key)==0)
	{
		memset(temp, '\0', sizeof(temp));
		sprintf(temp, "Your DVD key : %016llX%016llX\r\n", ld(&key[0x0]),ld(&key[0x8]));
		fuse_strcat(priv->data, temp);
	}
        
        memset(key, '\0', sizeof(key));
	if (get_virtual_cpukey(key)==0)
	{
		memset(temp, '\0', sizeof(temp));
		sprintf(temp, "Your virtual CPU key : %016llX%016llX\r\n", ld(&key[0x0]),ld(&key[0x8]));
		fuse_strcat(priv->data, temp);
	}

	priv->hdr_state = 0;
	priv->ptr = 0;
	priv->len = strlen(priv->data);
	priv->togo = priv->len;
	http->code = 200;
	return 1;
}

int response_fuse_do_header(struct http_state *http)
{
	struct response_fuse_priv_s *priv = http->response_priv;

	/*
	const char *t=0, *o=0;
	char buf[32];
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		o = "text/plain";
		break;
	case 1:
		t = "Content-Length";
		sprintf(buf, "%d", priv->len);
		o = buf;
		break;
	case 2:
		return httpd_do_std_header(http);
	}
	*/

	const char *t=0, *o=0;
	char buf[80];
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		o = "text/plain; charset=US-ASCII";
		break;
	case 1:
		t = "Content-Length";
		sprintf(buf, "%d", priv->len);
		o = buf;
		break;
	case 2:
		t = "Content-Transfer-Encoding";
		o = "7bit";
		break;
	case 3:
		t = "Content-Disposition";
		o = "attachment; filename=fuses.txt";
		break;
	case 4:
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

int response_fuse_do_data(struct http_state *http)
{
	struct response_fuse_priv_s *priv = http->response_priv;

	int av = httpd_available_sendbuffer(http);

	if (!av)
	{
		//printf("no httpd sendbuffer space\n");
		return 1;
	}

	if (av > (priv->len - priv->ptr))
		av = priv->len - priv->ptr;

	while (av)
	{
		int maxread = 1024;
		if (maxread > av)
			maxread = av;

		httpd_put_sendbuffer(http, (void*)(priv->data + priv->ptr), maxread);

		priv->ptr += maxread;
		av -= maxread;
		priv->togo-=maxread;

		if (priv->togo <= 0){
			//printf("%d bytes sent\n",priv->len);
			return 0;
		}

	}

	return 1;
}

void response_fuse_finish(struct http_state *http)
{
	struct response_fuse_priv_s *priv = http->response_priv;
	mem_free(priv->data);
	mem_free(priv);
}
