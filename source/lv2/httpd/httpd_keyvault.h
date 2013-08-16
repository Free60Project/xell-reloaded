/*
 * httpd_keyvault.h
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

#ifndef HTTPD_KEYVAULT2_H_
#define HTTPD_KEYVAULT2_H_

#include "httpd/httpd.h"

int response_keyvault_process_request(struct http_state *http, const char *method, const char *url);
int response_keyvault_do_header(struct http_state *http);
int response_keyvault_do_data(struct http_state *http);
void response_keyvault_finish(struct http_state *http);

#endif /* HTTPD_KEYVAULT2_H_ */

