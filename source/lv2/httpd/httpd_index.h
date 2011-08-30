/*
 * httpd_index.h
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

#ifndef HTTPD_INDEX_H_
#define HTTPD_INDEX_H_


#endif /* HTTPD_INDEX_H_ */

#include "httpd/httpd.h"

int response_index_process_request(struct http_state *http, const char *method, const char *url);
int response_index_do_header(struct http_state *http);
int response_index_do_data(struct http_state *http);
void response_index_finish(struct http_state *http);
