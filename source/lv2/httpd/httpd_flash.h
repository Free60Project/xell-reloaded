#ifndef __httpd_flash_h
#define __httpd_flash_h


int response_flash_process_request(struct http_state *http, const char *method, const char *url);
int response_flash_do_header(struct http_state *http);
int response_flash_do_data(struct http_state *http);
void response_flash_finish(struct http_state *http);

#endif
