/*
 * httpd_keyvault.c
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

#include <stdio.h>
#include <string.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xb360/xb360.h>
//#include <lwip/debug.h>
//#include <lwip/stats.h>
#include <lwip/tcp.h>
#include <network/network.h>

#include "httpd.h"
#include "httpd_index.h"

int rawBlockMode = 0, decrypted = 0;

struct response_mem_priv_s
{
	void *base;
	int len;
	int ptr, hdr_state;
	int togo;
	char *filename;
};

extern struct sfc sfc;

int response_keyvault_process_request(struct http_state *http, const char *method, const char *url)
{
	if (strcmp(method, "GET"))
		return 0;

	if (strcmp(url, "/KVRAW") && strcmp(url, "/KVRAW2") && strcmp(url, "/KV"))
		return 0;
	if (!(strcmp(url, "/KVRAW2") && !(strcmp(url, "/KV"))))
		rawBlockMode = 1;
	if (strcmp(url, "/KV"))
		decrypted = 1;

	http->response_priv = mem_malloc(sizeof(struct response_mem_priv_s));
	if (!http->response_priv)
		return 0;
	struct response_mem_priv_s *priv = http->response_priv;

	int bytes_sz = (KV_FLASH_SIZE / sfc.page_sz) * sfc.page_sz_phys;

	//priv->base = (void*) 0x80000200c8000000ULL;
	if (sfc.initialized == SFCX_INITIALIZED && !rawBlockMode)
		priv->base = (void*) mem_malloc(bytes_sz);
	else
		priv->base = (void*) mem_malloc(KV_FLASH_SIZE);

	if (priv->base == NULL) {
		printf("keyvault: Out of memory\n");
		return 0;
	}

	
	if (sfc.initialized == SFCX_INITIALIZED && !rawBlockMode && !decrypted)
	{
		int i = 0;
		for (i = 0; i < (bytes_sz / sfc.page_sz_phys); i++)		
			sfcx_read_page(priv->base + (i * sfc.page_sz_phys), KV_FLASH_SIZE + (i * sfc.page_sz), 1);		
	}
	else if (!decrypted)
		xenon_get_logical_nand_data(priv->base, KV_FLASH_OFFSET, KV_FLASH_SIZE);
	else
	{
		if(kv_read(priv->base,0)!=0) 
			if(kv_read(priv->base,1)!=0){ // Try decrypt with virtual cpukey 
				priv->hdr_state = HTTPD_SERVER_CLOSE;
				priv->ptr = 0;
				http->code = 500;
				http->code_ex = HTTPD_ERR_KV_READ;
				return 0; //TODO Set Internal Server Error??
			}
	}

	priv->hdr_state = 0;
	priv->ptr = 0;
	if (sfc.initialized == SFCX_INITIALIZED && !rawBlockMode)
		priv->len = bytes_sz;
	else
		priv->len = KV_FLASH_SIZE;
	priv->togo = priv->len;
	if (!decrypted)
		priv->filename="keyvault_raw.bin";
	else
		priv->filename="keyvault.bin";
	http->code = 200;

	return 1;
}

int response_keyvault_do_header(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;

	const char *t=0, *o=0;
	char buf[80];
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
		t = "Content-Transfer-Encoding";
		o = "binary";
		break;
	case 3:
		t = "Content-Disposition";
		sprintf(buf,"attachment; filename=%s",priv->filename);
		o = buf;
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

int response_keyvault_do_data(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;

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
		int maxread = priv->len;
		if (maxread > av)
			maxread = av;

		//printf("response_keyvault2_do_data httpd_put_sendbuffer\n");
		httpd_put_sendbuffer(http, (void*)(priv->base + priv->ptr), maxread);

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

void response_keyvault_finish(struct http_state *http)
{
	struct response_mem_priv_s *priv = http->response_priv;
	if (priv->base != NULL){
		mem_free(priv->base);
	}
	mem_free(priv);
}

