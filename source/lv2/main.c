#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <usb/usbmain.h>
#include <ppc/register.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <xb360/xb360.h>
#include <network/network.h>
#include <httpd/httpd.h>
#include <diskio/ata.h>
#include <elf/elf.h>
#include <version.h>
#include <byteswap.h>

#include "asciiart.h"
#include "config.h"
#include "zlib/xell_lib.h"
#include "tftp/tftp.h"
#include "kboot/kbootconf.h"

extern char dt_blob_start[];
extern char dt_blob_end[];
extern char *kboot_tftp;

const unsigned char elfhdr[] = {0x7f, 'E', 'L', 'F'};
const unsigned char cpiohdr[] = {0x30, 0x37, 0x30, 0x37};
const unsigned char kboothdr[] = "#KBOOTCONFIG";

void do_asciiart()
{
	char *p = asciiart;
	while (*p)
		console_putch(*p++);
}

void wait_and_cleanup_line()
{
	unsigned int w=0;
	console_get_dimensions(&w,NULL);
	
	char sp[w];

	memset(sp,' ',w);
	sp[w-1]='\0';

	uint64_t t=mftb();
	while(tb_diff_msec(mftb(),t)<200){ // yield to network
		network_poll();
	}
	
	printf("\r%s\r",sp);
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

void launch_elf(void * addr, unsigned len){
        int gzipped_initrd = 0;
	//check if addr point to a gzip file
	unsigned char * gzip_file = (unsigned char *)addr;
	if((gzip_file[0]==0x1F)&&(gzip_file[1]==0x8B)){
		//found a gzip file
		printf(" * Found a gzip file...\n");
		if(inflate_compare_header((char*)addr, len, cpiohdr, 4, 1) == 0){
			gzipped_initrd = 1;
			goto check_hdr;
		}
		char * dest = malloc(ELF_MAXSIZE);
		int destsize = 0;
		if(inflate_read((char*)addr, len, &dest, &destsize, 1) == 0){
			//relocate elf ...
			memcpy(addr,dest,destsize);
			printf(" * Successfully unpacked...\n");
			free(dest);
			len=destsize;
		}
		else{
			printf(" * Unpacking failed...\n");
			free(dest);
			return;
		}
	}
        
check_hdr:
	//Check elf header
	if (!memcmp(addr, elfhdr, 4))
	{
		printf(" * Found ELF...\n");
		elf_runWithDeviceTree(addr,len,dt_blob_start,dt_blob_end-dt_blob_start);
	}
	//Check kbootconf header
        else if (!memcmp(addr, kboothdr,12))
        {
                printf(" * Found kbootconfig header ...\n");
                try_kbootconf(addr,len);
        }
	//Check cpio header or initrd_found flag
        else if (!memcmp(addr,cpiohdr,4) || gzipped_initrd == 1)
        {
                printf(" * Found initrd/cpio file ...\n");
                kernel_prepare_initrd(addr,len);
        }
        else
                printf("! Bad header!\n");
}

int try_load_elf(char *filename)
{
	wait_and_cleanup_line();
	printf("Trying %s...",filename);
	
	int f = open(filename, O_RDONLY);
	if (f < 0)
	{
		return f;
	}

	struct stat s;
	fstat(f, &s);

	int size = s.st_size;
	void * buf=malloc(size);

	printf("\n * '%s' found, loading %d...\n",filename,size);
	int r = read(f, buf, size);
	if (r < 0)
	{
		close(f);
		free(buf);
		return r;
	}

	launch_elf(buf,r);

	free(buf);
	return 0;
}

char FUSES[350]; /* this string stores the ascii dump of the fuses */

unsigned char stacks[6][0x10000];

void reset_timebase_task()
{
	mtspr(284,0); // TBLW
	mtspr(285,0); // TBUW
	mtspr(284,0);
}

void synchronize_timebases()
{
	xenon_thread_startup();
	
	std(0x200611a0,0); // stop timebase
	
	int i;
	for(i=1;i<6;++i){
		xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
		while(xenon_is_thread_task_running(i));
	}
	
	reset_timebase_task(); // don't forget thread 0
			
	std(0x200611a0,0x1ff); // restart timebase
}
	
int main(){
	int i;

	// linux needs this
	synchronize_timebases();
	
	// irqs preinit (SMC related)
	*(volatile uint32_t*)0xea00106c = 0x1000000;
	*(volatile uint32_t*)0xea001064 = 0x10;
	*(volatile uint32_t*)0xea00105c = 0xc000000;

	xenon_smc_start_bootanim();
	xenon_smc_set_power_led(0, 0, 1);

	// flush console after each outputted char
	setbuf(stdout,NULL);

	xenos_init(VIDEO_MODE_AUTO);
#ifdef DEFAULT_THEME
	console_set_colors(CONSOLE_COLOR_BLUE,CONSOLE_COLOR_WHITE); // White text on blue bg
#else
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_GREEN); // Green text on black bg
#endif
	console_init();

	printf("\nXeLL - Xenon linux loader second stage " LONGVERSION "\n");

	do_asciiart();

	//delay(3); //give the user a chance to see our splash screen <- network init should last long enough...
	
	xenon_sound_init();

	printf(" * nand init\n");
	sfcx_init();
	if (sfc.initialized != SFCX_INITIALIZED)
	{
		printf(" ! sfcx initialization failure\n");
		printf(" ! nand related features will not be available\n");
		delay(5);
	}
	xenon_config_init();

	printf(" * network init\n");
	network_init();

	printf(" * starting httpd server...");
	httpd_start();
	printf("success\n");

	printf(" * usb init\n");
	usb_init();
	usb_do_poll();

	printf(" * sata hdd init\n");
	xenon_ata_init();

	printf(" * sata dvd init\n");
	xenon_atapi_init();

	/* display some cpu info */
	printf(" * CPU PVR: %08x\n", mfspr(287));

#ifndef NO_PRINT_CONFIG
	printf(" * FUSES - write them down and keep them safe:\n");
	char *fusestr = FUSES;
	for (i=0; i<12; ++i){
		u64 line;
		unsigned int hi,lo;

		line=xenon_secotp_read_line(i);
		hi=line>>32;
		lo=line&0xffffffff;

		fusestr += sprintf(fusestr, "fuseset %02d: %08x%08x\n", i, hi, lo);
	}
	printf(FUSES);

	print_cpu_dvd_keys();
	network_print_config();
#endif
	printf("\n * Looking for xenon.elf or vmlinux on USB/CD/DVD or user-defined file via TFTP...\n\n");
	for(;;){
		// try USB
		try_rawflash("uda:/updflash.bin");
		updateXeLL("uda:/updxell.bin");
		try_load_elf("uda:/kboot.conf");
		try_load_elf("uda:/xenon.elf");
		try_load_elf("uda:/xenon.z");
		try_load_elf("uda:/vmlinux");
		
		// try network
		wait_and_cleanup_line();
		printf("Trying TFTP %s:%s... ",boot_server_name(),boot_file_name());
		boot_tftp(boot_server_name(),boot_file_name());
		
		// try CD/DVD
		try_rawflash("dvd:/updflash.bin");
		updateXeLL("dvd:/updxell.bin");
		try_load_elf("dvd:/kboot.conf");
		try_load_elf("dvd:/xenon.elf");
		try_load_elf("dvd:/xenon.z");
		try_load_elf("dvd:/vmlinux"); 

		// try Hard Drive
		try_rawflash("sda:/updflash.bin");
		updateXeLL("sda:/updxell.bin");
		try_load_elf("sda:/kboot.conf");
		try_load_elf("sda:/xenon.elf");
		try_load_elf("sda:/xenon.z");
		try_load_elf("sda:/vmlinux");

		//subsystem servicing
		usb_do_poll();
		network_poll();
	}

	return 0;
}

