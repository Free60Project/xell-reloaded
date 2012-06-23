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
#include <sys/iosupport.h>
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
#include "file.h"
#include "tftp/tftp.h"

void do_asciiart()
{
	char *p = asciiart;
	while (*p)
		console_putch(*p++);
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

	mount_all_devices();

	findDevices();

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
		
		fileloop();
		
		// try network
		wait_and_cleanup_line();
		printf("Trying TFTP %s:%s... ",boot_server_name(),boot_file_name());
		boot_tftp(boot_server_name(),boot_file_name());

		//subsystem servicing
		usb_do_poll();
		network_poll();
	}

	return 0;
}

