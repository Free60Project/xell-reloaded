#include <stdlib.h>
#include <xetypes.h>

#include <lwip/mem.h>
#include <lwip/memp.h>
#include <lwip/sys.h>

#include <lwip/stats.h>

#include <lwip/inet.h>
#include <lwip/ip.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <netif/etharp.h>

#include <ppc/timebase.h>
#include <network/network.h>
#include <elf/elf.h>
#include "config.h"
#include "file.h"

#define TFTP_STATE_RRQ_SEND 0
#define TFTP_STATE_ACK_SEND 1
#define TFTP_STATE_FINISH   2

#define TFTP_OPCODE_RRQ   1
#define TFTP_OPCODE_WRQ   2
#define TFTP_OPCODE_DATA  3
#define TFTP_OPCODE_ACK   4
#define TFTP_OPCODE_ERROR 5

static int tftp_state, tftp_result;
static int maxtries, tries, current_block;
static int send, last_size, ptr;
static uint64_t ts, start;
static unsigned char *base;
static int image_maxlen;

extern char *kboot_tftp;
extern void console_clrline();

static void tftp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
	unsigned char *d = p->payload;
	if (p->tot_len < 2)
	{
		tftp_state = TFTP_STATE_FINISH;
		tftp_result = -1;
	}	
	start=mftb();
	
	switch ((d[0] << 8) | d[1])
	{
	case 3: // DATA
	{
		int this_block = (d[2] << 8) | d[3];
		int pl = p->tot_len - 4;
		if (this_block == current_block + 1)
		{
			if ((ptr + pl) <= image_maxlen)
				memcpy(base + ptr, d + 4, pl);
			current_block++;
			
			if (!(current_block & 255))
				printf("%c                                                                       \r", "|/-\\"[(current_block>>8)&3]);

			ptr += pl;
			if (pl < last_size)
			{
				tftp_result = 0;
			 	tftp_state = TFTP_STATE_FINISH;
			 	break;
			}
		} else
		{
			console_clrline();
			printf("tftp: out of sequence block! (got %d, expected %d)\n", this_block, current_block + 1);
			if (this_block == current_block)
			{
				printf("dupe.\n");
				break;
			}
		}

		last_size = pl;
		send = 1;
		break;
	}
	case 5: // ERROR
		//printf("TFTP got ERROR\n");
		tftp_state = TFTP_STATE_FINISH;
			/* please don't overflow this. */
		console_clrline();
		printf("tftp error %d: %s", (d[2] << 8) | d[3], d + 4);
		tftp_result = -2;
		break;
	}
	
	if (tftp_state == TFTP_STATE_RRQ_SEND)
	{
		udp_connect(pcb, addr, port);
		ts=mftb();
		tftp_state = TFTP_STATE_ACK_SEND;
	}
	
	tries = 0;
	pbuf_free(p);
}

int send_ack(struct udp_pcb *pcb)
{
				struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);

				if (!p)
				{
					printf("internal error: out of memory!\n");
					return -1;
				}
				
				unsigned char *d = p->payload;
				
				*d++ = 0;
				*d++ = TFTP_OPCODE_ACK;
				*d++ = current_block >> 8;
				*d++ = current_block & 0xFF;
				
				if (udp_send(pcb, p))
				{
					printf("TFTP: packet send error.");
					pbuf_free(p);
					return -1;
				}
				pbuf_free(p);
				

				return 0;
}

int do_tftp(void *target, int maxlen, struct ip_addr server, const char *file)
{
/*	printf("TFTP boot from %u.%u.%u.%u:%s\n", 
		ip4_addr1(&server), ip4_addr2(&server), ip4_addr3(&server), ip4_addr4(&server),
		file);*/
	
	base = (unsigned char*)target;
	image_maxlen = maxlen;
	
	struct udp_pcb *pcb = udp_new();
	if (!pcb)
	{
		printf("internal error: out of memory (udp)\n");
		return -1;
	}
	
	udp_bind(pcb, IP_ADDR_ANY, htons(0x1234));
	udp_recv(pcb, tftp_recv, 0);
	
	tftp_state = TFTP_STATE_RRQ_SEND;
	current_block = 0;
	last_size = 512;//Last size has to be 512 or greater - this should fix files smaller than 1 DATA packet
	ptr = 0;
	
	start=mftb();
	
	send = 1;
	
	maxtries = 1;
	tries = 0;

	while (tftp_state != TFTP_STATE_FINISH)
	{
		uint64_t now;
		now=mftb();
		if (tb_diff_msec(now, start) > 500)
		{
			if (tftp_state == TFTP_STATE_RRQ_SEND)
			{
				console_clrline();
				printf("TFTP: no answer from server");
			}
			else
			{
				if (!current_block)
					tftp_state = TFTP_STATE_RRQ_SEND;
				console_clrline();
				printf("TFTP: packet lost (%d)\n", current_block);
			}
			tries++;
			if (tries >= maxtries / 2)
			{
				if (tftp_state != TFTP_STATE_RRQ_SEND)
					printf("retry.\n");
				tftp_state = TFTP_STATE_RRQ_SEND;
				current_block = 0;
				last_size = 0;
			}
			if (tries >= maxtries)
			{
				//printf("%d tries exceeded, aborting.\n", maxtries);
				tftp_result = -2;
				break;
			}
			start=mftb();
			send = 1;
		}
		if (send)
		{
			switch (tftp_state)
			{
			case TFTP_STATE_RRQ_SEND:
			{
				struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 2 + strlen(file) + 1 + 6, PBUF_RAM);

				if (!p)
				{
					printf("internal error: out of memory! (couldn't allocate %d bytes)\n", (int)(2 + strlen(file) + 1 + 6));
					break;
				}
				
				unsigned char *d = p->payload;
				
				*d++ = 0;
				*d++ = TFTP_OPCODE_RRQ;
				strcpy((char*)d, file);
				d += strlen(file) + 1;
				strcpy((char*)d, "octet");
				d += 6;
				
				if (udp_sendto(pcb, p, &server, 69))
				{
					console_clrline();
					printf("TFTP: packet send error.");
				}
				pbuf_free(p);
				send = 0;
				break;
			}
			case TFTP_STATE_ACK_SEND:
			{
				if(send_ack(pcb) != 0)
				{
					udp_remove(pcb);
					return -1;
				}
				send = 0;
				break;
			}
			default:
				tftp_state = TFTP_STATE_FINISH;
			}
		}
		network_poll();
	}
	
	//printf("tftp result: %d\n", tftp_result);
	if (!tftp_result)
	{
		if(send_ack(pcb) != 0)
		{
			udp_remove(pcb);
			return -1;
		}
		uint64_t end;
		end=mftb();
		console_clrline();
		printf("%d packets (%d bytes, %d packet size), received in %dms, %d kb/s\n",
			current_block, ptr, last_size, (int)tb_diff_msec(end, ts), 
			(int)(ptr / 1024 * 1000 / tb_diff_msec(end, ts)));
	}
		
	udp_remove(pcb);
	
	return (tftp_result < 0) ? tftp_result : ptr;
}


int boot_tftp(const char *server_addr, const char *tftp_bootfile, int filetype)
{
	int ret;
	char *args = strchr(tftp_bootfile, ' ');
	if (args)
		*args++ = 0;

	//const char *msg = " was specified, neither manually nor via dhcp. aborting tftp.\n";
	
	ip_addr_t server_address;
	//printf(server_addr);
	if (!ipaddr_aton(server_addr, &server_address))
	{
		console_clrline();
		printf("no server address given");
		server_address.addr = 0;
	}
	
	if (!server_address.addr)
	{
		console_clrline();
		printf("no tftp server address");
		//printf(msg);
		return -1;
	}
	
	if (!(tftp_bootfile && *tftp_bootfile))
	{
		console_clrline();
		printf("no tftp bootfile name");
		//printf(msg);
		return -1;
	}
	//printf(" * loading tftp bootfile '%s:%s'\n", server_addr, tftp_bootfile);
	
	void * elf_raw=malloc(ELF_MAXSIZE);
	
	int res = do_tftp(elf_raw, ELF_MAXSIZE, server_address, tftp_bootfile);
	if (res < 0){
		free(elf_raw);
		return res;
	}
	
	if (filetype == TYPE_ELF) {
		char * argv[] = {
			tftp_bootfile,
		};
		int argc = sizeof (argv) / sizeof (char *);
		
		elf_setArgcArgv(argc, argv);
	}
	
	ret = launch_file(elf_raw,res,filetype);
	
	free(elf_raw);
	return ret;
}

extern int boot_tftp_url(const char *url)
{
	const char *bootfile = url;
	
	char server_addr[20];
	
	if (!bootfile)
		bootfile = "";
	
	// ip:/path
	// /path
	// ip
	
	const char *r = strchr(bootfile, ':');
	
	if (r)
	{
		int l = r - bootfile;
		if (l > 19)
			l = 19;
		memcpy(server_addr, bootfile, l);
		server_addr[l] = 0;
		bootfile = r + 1;
	} else
	{
		*server_addr = 0;
		bootfile = url;
	}
	
	return boot_tftp(server_addr, bootfile, TYPE_ELF);
}

char *boot_server_name()
{       
	if (kboot_tftp && kboot_tftp[0])
		return kboot_tftp;
            
	if (netif.dhcp && netif.dhcp->boot_server_name[0])
    	return netif.dhcp->boot_server_name;
	
	if (netif.dhcp && netif.dhcp->offered_si_addr.addr)
    	return ipaddr_ntoa(&netif.dhcp->offered_si_addr);

	return "192.168.1.98";
}

char *boot_file_name()
{
	if (netif.dhcp && *netif.dhcp->boot_file_name)
		return netif.dhcp->boot_file_name;

	return "/tftpboot/xenon";
}
