/*  main.c - kboot.conf parsing

Copyright (C) 2010-2011  Hector Martin "marcan" <hector@marcansoft.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#ifndef KBOOTCONF_H
#define KBOOTCONF_H

#define MAX_CMDLINE_SIZE 255
#define MAX_KBOOTCONF_SIZE 16384
#define MAX_KERNELS 64

struct kbootkernel {
	char *label;
	char *kernel;
	char *initrd;
	char *root;
        char *video;
	char *parameters;
};

struct kbootconf {
	int timeout;
        int videomode;
	int speedup;
	char *msgfile;
        char *tftp_server;
        char *ipaddress;
        char *netmask;
        char *gateway;
	int default_idx;
	int num_kernels;
	struct kbootkernel kernels[MAX_KERNELS];
};

int kbootconf_parse(void);
int try_kbootconf(void * addr, unsigned len);

#endif
