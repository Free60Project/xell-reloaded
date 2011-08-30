#ifndef __tftp_h
#define __tftp_h

#include <lwip/ip.h>

extern int do_tftp(struct ip_addr server, const char *file);
extern int boot_tftp(const char *server_addr, const char *filename);
extern int boot_tftp_url(const char *url);

#endif
