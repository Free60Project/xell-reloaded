/*
 * httpd_flash.c
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

//#include "lwip/debug.h"
//#include "lwip/stats.h"
#include <lwip/tcp.h>

#include <string.h>
#include <xenon_nand/xenon_sfcx.h>

#include "network/network.h"
#include "crypt/hmac_sha1.h"
#include "crypt/rc4.h"
#include "xb360/xb360.h"
#include "httpd.h"
#include "httpd_flash.h"

struct response_flash_priv_s
{
	int pages;
	int page, hdr_state;
	int togo;
};

extern struct sfc sfc;

unsigned char buffer[0x4200]; //we only use a page (0x210)

int response_flash_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;

	if (strcmp(url, "/FLASH"))
		return 0;

	http->response_priv = mem_malloc(sizeof(struct response_flash_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_flash_priv_s *priv = http->response_priv;

	int pages = sfc.size_pages;
	if(pages == 0 ){
		priv->hdr_state = HTTPD_SERVER_CLOSE;
		priv->page = 0;
		priv->pages = 0;
		priv->togo = 0;
		http->code = 500;
		return 0; //TODO Set Internal Server Error??
	}

	priv->hdr_state = 0;
	priv->page = 0; 							//Current Page Number
	priv->pages = pages; 						//Number of Pages
	priv->togo = priv->pages;
	http->code = 200;
	return 1;
}

int response_flash_do_header(struct http_state *http)
{
	struct response_flash_priv_s *priv = http->response_priv;

	const char *t=0, *o=0;
	char buf[80];
	switch (priv->hdr_state)
	{
	case 0:
		t = "Content-Type";
		//o = "application/binary";
		o = "application/octet-stream";
		break;
	case 1:
		t = "Content-Length";
		sprintf(buf, "%d", priv->pages * sfc.page_sz_phys);
		o = buf;
		break;
	//case 2:
	//	t = "Content-Transfer-Encoding";
	//	o = "binary";
	//	break;
	case 2:
		t = "Content-Disposition";
		//TODO Detect Dev/Ret and also Version Num, Make part of Filename
		//o = "attachment; filename=raw_flash.bin";
		//sprintf(buf, "attachment; filename=raw_flash_%d.bin", GetHvVersion());
		sprintf(buf, "attachment; filename=flashdmp.bin");
		o = buf;
		break;
	case 3:
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

int response_flash_do_data(struct http_state *http)
{
	struct response_flash_priv_s *priv = http->response_priv;


	int av = httpd_available_sendbuffer(http);

	if (!av)
	{
		printf("no httpd sendbuffer space\n");
		return 1;
	}

	while (av >= sfc.page_sz_phys)
	{
		//int maxread = sfc.page_sz_phys;
		//if (maxread > av)
		//	maxread = av;

		sfcx_read_page(buffer, priv->page * sfc.page_sz, 1);
		httpd_put_sendbuffer(http, (void*)buffer, sfc.page_sz_phys);

		priv->page++;
		priv->togo--;
		av -= sfc.page_sz_phys;

		if (priv->togo <= 0){
			//advance to next stage (close connection)
			return 0;
		}
	}
	return 1;
}

void response_flash_finish(struct http_state *http)
{
	struct response_flash_priv_s *priv = http->response_priv;
	mem_free(priv);
}
