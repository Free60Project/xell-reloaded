#ifndef __tftp_h
#define __tftp_h

#include <lwip/ip.h>

extern int do_tftp(struct ip_addr server, const char *file);
extern int boot_tftp(ip_addr_t server_addr, const char *filename, int filetype);
extern int boot_tftp_url(const char *url);
ip_addr_t boot_server_name();
char *boot_file_name();

#endif
