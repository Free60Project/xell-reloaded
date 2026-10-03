#ifndef __tftp_h
#define __tftp_h

#include <lwip/ip.h>

extern int do_tftp(ip_addr_t server, const char *file);
extern int boot_tftp(ip_addr_t server_addr, const char *filename, int filetype);
extern int boot_tftp_url(const char *url);
ip_addr_t boot_server_name(ip_addr_t *fallback_address);
char *boot_file_name();

#endif
