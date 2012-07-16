/*  kbootcfg.c - kboot.cfg parsing

Copyright (C) 2010-2011  Hector Martin "marcan" <hector@marcansoft.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

/* See http://www.kernel.org/pub/linux/kernel/people/geoff/cell/ps3-linux-docs/ps3-linux-docs-08.06.09/mini-boot-conf.txt */

/* Menu functions written by Georg "ge0rg" Lukas <georg@op-co.de> for XeLL (Menu-branch)
   kbootconf-parsing and menu functions modified by tuxuser <tuxuser360@gmail.com> */

#include <stdlib.h>
#include <string.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_uart/xenon_uart.h>
#include <input/input.h>
#include <lwip/ip_addr.h>
#include <network/network.h>
#include <xenos/xenos.h>
#include <ppc/timebase.h>
#include <elf/elf.h>
#include <usb/usbmain.h>
#include <console/console.h>
#include <xenon_soc/xenon_power.h>

#include "tftp/tftp.h"
#include "kbootconf.h"
#include "file.h"

int boot_entry;
char conf_buf[MAX_KBOOTCONF_SIZE];
struct kbootconf conf;

ip_addr_t oldipaddr, oldnetmask, oldgateway;
char *kboot_tftp;

enum ir_remote_codes IR;
static struct controller_data_s ctrl;
static struct controller_data_s old_ctrl;

/* network.h */
extern struct netif netif;

char *strip(char *buf)
{
	while (*buf == ' ' || *buf == '\t')
		buf++;
	char *end = buf + strlen(buf) - 1;
	while (*end == ' ' || *end == '\t')
		*end-- = 0;

	return buf;
}

void split(char *buf, char **left, char **right, char delim)
{
	char *p = strchr(buf, delim);

	if (p) {
		*p = 0;
		*left = strip(buf);
		*right = strip(p+1);
	} else {
		*left = strip(buf);
		*right = NULL;
	}
}

int kboot_loadfile(char *filename, int type)
{
	int ret;
	/* If filename includes ':' it's seen as valid mountname */
	if(strrchr(filename,':')!= NULL)
		ret = try_load_file(filename,type);
	else
		ret = boot_tftp(boot_server_name(),filename,type);
		
	return ret;
}

void kboot_set_config(void)
{
        
        int setnetconfig = 0;
        static int oldvideomode = -1;
        ip_addr_t ipaddr, netmask, gateway, tftpserver;
        
	if(conf.tftp_server != NULL)
		if (ipaddr_aton(conf.tftp_server,&tftpserver))
			kboot_tftp = conf.tftp_server;

        /* Only reinit network if IPs dont match which got set by kboot on previous try*/
	if(conf.ipaddress != NULL)
        	if (ipaddr_aton(conf.ipaddress,&ipaddr) && ip_addr_cmp(&oldipaddr,&ipaddr) == 0)
        	{
        	        printf(" * taking network down to set config values\n");
        	        setnetconfig = 1;
        	        netif_set_down(&netif);
                
        	        netif_set_ipaddr(&netif,&ipaddr);
        	        ip_addr_set(&oldipaddr,&ipaddr);
        	}

	if(conf.netmask != NULL)
        	if (ipaddr_aton(conf.netmask,&netmask) && setnetconfig){
        	        netif_set_netmask(&netif,&netmask);
        	        ip_addr_set(&oldnetmask,&netmask);
        	}
        
	if(conf.gateway != NULL)
        	if (ipaddr_aton(conf.gateway,&gateway) && setnetconfig){
        	        netif_set_gw(&netif,&gateway);
        	        ip_addr_set(&oldgateway,&gateway); 
        	}
        
        if (setnetconfig){
           printf(" * bringing network back up...\n");
           netif_set_up(&netif);
           network_print_config();
        }
        
        if(conf.videomode > VIDEO_MODE_AUTO && conf.videomode <= VIDEO_MODE_NTSC && oldvideomode != conf.videomode){
            oldvideomode = conf.videomode;
            xenos_init(conf.videomode);
	    console_init();
            printf(" * Xenos re-initalized\n");
        }
	
	if(conf.speedup >= XENON_SPEED_FULL && conf.speedup <= XENON_SPEED_1_3){ //speedmode: drivers/xenon_soc/xenon_power.h
		printf("Speeding up CPU\n");
		xenon_make_it_faster(conf.speedup);
	}
        
}

int kbootconf_parse(void)
{
	char *lp = conf_buf;
	char *dflt = NULL;
	char *dinitrd = NULL;
	char *droot = NULL;
        char *dvideo = NULL;
	char tmpbuf[MAX_CMDLINE_SIZE];
	int lineno = 1;
	int i;

	memset(&conf, 0, sizeof(conf));

	conf.timeout = -1;
        conf.videomode = -1;
	conf.speedup = 0;

	while(*lp) {
		char *newline = strchr(lp, '\n');
		char *next;
		if (newline) {
			*newline = 0;
			next = newline+1;
		} else {
			next = lp+strlen(lp);
		}

		lp = strip(lp);
		if (!*lp)
			goto nextline;

		char *left, *right;

		split(lp, &left, &right, '=');
                if (!right) {
                    if(strncmp(left,"#",1) && strncmp(left,";",1))
			PRINT_WARN("kboot.conf: parse error (line %d)\n", lineno);
                    goto nextline;
		}

		while(*right == '"' || *right == '\'')
			right++;
		char *rend = right + strlen(right) - 1;
		while(*rend == '"' || *rend == '\'')
			*rend-- = 0;

		if (!strcmp(left, "timeout")) {
			conf.timeout = atoi(right);
		} else if (!strcmp(left, "default")) {
			dflt = right;
		} else if (!strcmp(left, "message")) {
			conf.msgfile = right;
		} else if (!strcmp(left, "initrd")) {
			dinitrd = right;
		} else if (!strcmp(left, "root")) {
			droot = right;
		} else if (!strcmp(left, "videomode")) {
			conf.videomode = atoi(right);
		} else if (!strcmp(left, "speedup")) {
			conf.speedup = atoi(right);
                } else if (!strcmp(left, "tftp_server")) {
			conf.tftp_server = right;
                } else if (!strcmp(left, "ip")) {
			conf.ipaddress = right;
                } else if (!strcmp(left, "netmask")) {
			conf.netmask = right;
                } else if (!strcmp(left, "gateway")) {
			conf.gateway = right;
                } else if (!strncmp(left, "#", 1)||!strncmp(left, ";", 1)) {
			goto nextline;
		} else {
			if (strlen(right) > MAX_CMDLINE_SIZE) {
				PRINT_WARN("kboot.conf: maximum length exceeded (line %d)\n", lineno);
				goto nextline;
			}
			conf.kernels[conf.num_kernels].label = left;
                        conf.kernels[conf.num_kernels].kernel = right;
                        
			char *p = strchr(right, ' ');
			if (!p) {
				// kernel, no arguments
				conf.num_kernels++;
				goto nextline;
			}
			*p++ = 0;
			char *buf = p;
			char *root = NULL;
			char *initrd = NULL;
			tmpbuf[0] = 0;
			/* split commandline arguments and extract the useful bits */
			while (*p) {
				char *spc = strchr(p, ' ');
				if (spc)
					*spc++ = 0;
				else
					spc = p+strlen(p);
				if (*p == 0) {
					p = spc;
					continue;
				}

				char *arg, *val;
				split(p, &arg, &val, '=');
				if (!val) {
					strlcat(tmpbuf, arg, sizeof(tmpbuf));
					strlcat(tmpbuf, " ", sizeof(tmpbuf));
				} else if (!strcmp(arg, "root")) {
					root = val;
				} else if (!strcmp(arg, "initrd")) {
					initrd = val;
                                } else {
					strlcat(tmpbuf, arg, sizeof(tmpbuf));
					strlcat(tmpbuf, "=", sizeof(tmpbuf));
					strlcat(tmpbuf, val, sizeof(tmpbuf));
					strlcat(tmpbuf, " ", sizeof(tmpbuf));
				}
				
				p = spc;
			}

			int len = strlen(tmpbuf);
			if (len && tmpbuf[len-1] == ' ')
				tmpbuf[--len] = 0;
			len++;

			// UGLY: tack on initrd and root onto tmpbuf, then copy it entirely
			// on top of the original buffer (avoids having to deal with malloc)
			conf.kernels[conf.num_kernels].parameters = buf;
			if (initrd) {
				strlcpy(tmpbuf+len, initrd, sizeof(tmpbuf)-len);
				conf.kernels[conf.num_kernels].initrd = buf + len;
				len += strlen(initrd)+1;
			}
			if (root) {
				strlcpy(tmpbuf+len, root, sizeof(tmpbuf)-len);
				conf.kernels[conf.num_kernels].root = buf + len;
				len += strlen(root)+1;
			}
			memcpy(buf, tmpbuf, len);
			conf.num_kernels++;
		}

nextline:
		lp = next;
		lineno++;
	}

	conf.default_idx = 0;
	for (i=0; i<conf.num_kernels; i++) {
		if (dflt && !strcmp(conf.kernels[i].label, dflt))
			conf.default_idx = i;
		if (!conf.kernels[i].initrd && dinitrd)
		    conf.kernels[i].initrd = dinitrd;
		if (!conf.kernels[i].root && droot)
			conf.kernels[i].root = droot;
                if (!conf.kernels[i].video && dvideo)
			conf.kernels[i].video = dvideo;
//		if (conf.kernels[i].initrd && !conf.kernels[i].root)
//			conf.kernels[i].root = "/dev/ram0";
	}

#if 0
	printf("==== kboot.conf dump ====\n");
	if (conf.timeout != -1)
		printf("Timeout: %d\n", conf.timeout);
	if (conf.msgfile)
		printf("Message: %s\n", conf.msgfile);
        if (conf.tftp_server)
		printf("TFTP-Server: %s\n", conf.tftp_server);
        if (conf.ipaddress)
		printf("IP: %s\n", conf.ipaddress);
        if (conf.netmask)
                printf("Netmask: %s\n", conf.netmask);
        if (conf.gateway)
                printf("Gateway: %s\n", conf.gateway);
        if (conf.videomode)
		printf("Videomode: %i\n", conf.videomode);

	for (i=0; i<conf.num_kernels; i++) {
		printf("Entry #%d '%s'", i, conf.kernels[i].label);
		if (conf.default_idx == i)
			printf(" (default):\n");
		else
			printf(":\n");
		printf(" Kernel: %s\n", conf.kernels[i].kernel);
		if (conf.kernels[i].initrd)
			printf(" Initrd: %s\n", conf.kernels[i].initrd);
		if (conf.kernels[i].root)
			printf(" Root: %s\n", conf.kernels[i].root);
                if (conf.kernels[i].bdev)
			printf(" Bdev: %s\n", conf.kernels[i].bdev);
                if (conf.kernels[i].video)
			printf(" Video: %i\n", conf.kernels[i].video);
		if (conf.kernels[i].parameters)
			printf(" Parameters: %s\n", conf.kernels[i].parameters);
	}

	printf("=========================\n");
#endif
            
	return conf.num_kernels;
}

/** @brief presents a prompt on screen and waits until a choice is taken
*
* @param defaultchoice what to return on timeout
* @param max maximum allowed choice
* @param timeout number of seconds to wait
*
* @return choice (0 <= choice <= max)
*/
int user_prompt(int defaultchoice, int max, int timeout) {
   int redraw = 1;
   int min = 0;
   int delta, olddelta = -1;
   int timeout_disabled = 0;
   int old_default = defaultchoice;
   uint64_t start;
   
    start = mftb();
    delta = 0;
    
    if (defaultchoice < 0) defaultchoice = 0;
    
    /* Remove possibly cached input on UART */
    while (kbhit())
	getch();

    while (delta <= timeout || timeout_disabled) {
        
       /* measure seconds since menu start */
       delta = tb_diff_sec(mftb(), start);
       /* test if prompt update is needed */
       if (delta != olddelta) {
         olddelta = delta;
         redraw = 1;
       }
       /* redraw prompt - clear line, then print prompt */
       if (redraw) {
	if(timeout_disabled)
	{
         printf("                                                           \r[--] > %i",
         defaultchoice);
	}
	else
	{
         printf("                                                           \r[%02d] > %i",
         timeout - delta,defaultchoice);
	}
         redraw = 0;
     }

     if (xenon_smc_poll() == 0) {
       int ch = xenon_smc_get_ir();

        if (ch >= min && ch <= max)
                return ch;
        else if (ch == IR_UP && (defaultchoice < max-1))
                defaultchoice++;
        else if(ch == IR_DOWN && (defaultchoice > min))
                defaultchoice--;
        else if(ch == IR_OK)
                return defaultchoice;
        else if(ch == IR_BTN_B)
                return -1;  
        redraw = 1;   
      }

      if (kbhit()) {
	char ch = getch();

	if (ch == 0xD)
	  return defaultchoice;
        else if (ch == 0x41 && (defaultchoice < max-1)) // UP
          defaultchoice++;
        else if(ch == 0x42 && (defaultchoice > min)) // DOWN
          defaultchoice--;
        else if(ch == 0x63) // C - (c)ancel
          return -1;
        
        redraw = 1;
      }

       if (get_controller_data(&ctrl, 0)) {
         if ((ctrl.a > old_ctrl.a) || (ctrl.start > old_ctrl.start))
             return defaultchoice;
         else if ((ctrl.b > old_ctrl.b) || (ctrl.back > old_ctrl.back))
             return -1;
         else if ((ctrl.up > old_ctrl.up) && (defaultchoice < max-1))
             defaultchoice++;
         else if ((ctrl.down > old_ctrl.down) && (defaultchoice > min))
             defaultchoice--;
        old_ctrl=ctrl;
        redraw = 1;
        }

    network_poll();
    usb_do_poll();

    if(old_default != defaultchoice)
	timeout_disabled = 1;
    }
printf("\nTimeout.\n");
return defaultchoice;
}

int try_kbootconf(void * addr, unsigned len){
    int ret;
    if (len > MAX_KBOOTCONF_SIZE)
    {
        PRINT_ERR("file is bigger than %u bytes\n",MAX_KBOOTCONF_SIZE);
        PRINT_ERR("Aborting\n");
        return -1;
    }
    
    memcpy(conf_buf,addr,len);
    conf_buf[len] = 0; //ensure null-termination
    
    kbootconf_parse();
    kboot_set_config();
    
    if (conf.num_kernels == 0){
       PRINT_WARN("No kernels found in kboot.conf !\n");
       return -1;
    }
    
    int i;
    printf("\nXeLL Main Menu. Please choose from:\n\n");
    for(i=0;i<conf.num_kernels;i++)
    {
      printf("  <%i> %s - %s\n",i,conf.kernels[i].label, conf.kernels[i].kernel);
    }
    boot_entry = user_prompt(conf.default_idx, conf.num_kernels,conf.timeout);
    
    if (boot_entry < 0)
    {
        printf("\rAborted by user!\n");
        return boot_entry;
    }
    printf("\nYou chose: %i\n",boot_entry);
    
    if (conf.kernels[boot_entry].parameters)
        kernel_build_cmdline(conf.kernels[boot_entry].parameters,conf.kernels[boot_entry].root);

    kernel_reset_initrd();
        
    if (conf.kernels[boot_entry].initrd)
    {
        printf("Loading initrd ... ");
        ret = kboot_loadfile(conf.kernels[boot_entry].initrd,TYPE_INITRD);
        if (ret < 0) {
			printf("Failed!\nAborting!\n");
			return -1;
		}
		else
			printf("OK!\n");
    }

    printf("Loading kernel ...\n");
    ret = kboot_loadfile(conf.kernels[boot_entry].kernel,TYPE_ELF);
    if (ret < 0)
		printf("Failed!\n");
                
    memset(conf_buf,0,MAX_KBOOTCONF_SIZE);
    conf.num_kernels = 0;
    conf.timeout = 0;
    conf.default_idx = 0;
    conf.speedup = 0;
    
    return ret;
}
