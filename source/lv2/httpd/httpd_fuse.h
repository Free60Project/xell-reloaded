/*
 * httpd_fuse.h
 *
 *  Created on: Aug 3, 2008
 *      Author: Redline99
 */

#ifndef HTTPD_FUSE_H_
#define HTTPD_FUSE_H_


#endif /* HTTPD_FUSE_H_ */

#include "httpd/httpd.h"

int response_fuse_process_request(struct http_state *http, const char *method, const char *url);
int response_fuse_do_header(struct http_state *http);
int response_fuse_do_data(struct http_state *http);
void response_fuse_finish(struct http_state *http);
