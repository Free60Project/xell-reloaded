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
#include <xenon_soc/xenon_power.h>
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

#include "log.h"

void do_asciiart()
{
	char *p = asciiart;
	while (*p)
		console_putch(*p++);
	printf(asciitail);
}

void dumpana() {
	int i;
	for (i = 0; i < 0x100; ++i)
	{
		uint32_t v;
		xenon_smc_ana_read(i, &v);
		printf("0x%08x, ", (unsigned int)v);
		if ((i&0x7)==0x7)
			printf(" // %02x\n", (unsigned int)(i &~0x7));
	}
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

	std((void*)0x200611a0,0); // stop timebase

	int i;
	for(i=1;i<6;++i){
		xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
		while(xenon_is_thread_task_running(i));
	}

	reset_timebase_task(); // don't forget thread 0

	std((void*)0x200611a0,0x1ff); // restart timebase
}

int main(){
	LogInit();
	int i;

	printf("ANA Dump before Init:\n");
	dumpana();

	// linux needs this
	synchronize_timebases();

	// irqs preinit (SMC related)
	*(volatile uint32_t*)0xea00106c = 0x1000000;
	*(volatile uint32_t*)0xea001064 = 0x10;
	*(volatile uint32_t*)0xea00105c = 0xc000000;

	xenon_smc_start_bootanim();

	// flush console after each outputted char
	setbuf(stdout,NULL);

	xenos_init(VIDEO_MODE_AUTO);

	printf("ANA Dump after Init:\n");
	dumpana();

#ifdef SWIZZY_THEME
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_ORANGE); // Orange text on black bg
#elif defined XTUDO_THEME
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_PINK); // Pink text on black bg
#elif defined DEFAULT_THEME
	console_set_colors(CONSOLE_COLOR_BLUE,CONSOLE_COLOR_WHITE); // White text on blue bg
#else
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_GREEN); // Green text on black bg
#endif
	console_init();

	printf("\nXeLL - Xenon linux loader second stage " LONGVERSION "\n");
    printf("\nBuilt with GCC " GCC_VERSION " and Binutils " BINUTILS_VERSION " \n");
	do_asciiart();

	//delay(3); //give the user a chance to see our splash screen <- network init should last long enough...

	xenon_sound_init();

	xenon_make_it_faster(XENON_SPEED_FULL);
	if (xenon_get_console_type() != REV_CORONA_PHISON) //Not needed for MMC type of consoles! ;)
	{
		printf(" * nand init\n");
		sfcx_init();
		if (sfc.initialized != SFCX_INITIALIZED)
		{
			printf(" ! sfcx initialization failure\n");
			printf(" ! nand related features will not be available\n");
			delay(5);
		}
	}

	xenon_config_init();

#ifndef NO_NETWORKING

	printf(" * network init\n");
	network_init();

	printf(" * starting httpd server...");
	httpd_start();
	printf("success\n");

	ip_addr_t fallback_address;
	ip4_addr_set_u32(&fallback_address, 0xC0A8015A); // 192.168.1.90

	// Check if the fallback TFTP server or bootp server (usually a router) is reachable on port 69.
	// If not, we skip trying to download from it in the main loop. Relocating this code to this ifndef block
	// also ensures that if networking is disabled at the config level, we don't try to check for reachability.
	int fallback_reachable = is_tftp_server_reachable(fallback_address);
	if (fallback_reachable) {
		printf(" * Fallback TFTP server reachable!\n");
	} else {
		printf(" * Fallback TFTP server not reachable, skipping.\n");
	}

	int boot_server_reachable = is_tftp_server_reachable(boot_server_name());
	if(boot_server_reachable) {
		printf(" * Bootp server reachable!\n");
	} else {
		printf(" * Bootp server not reachable, skipping.\n");
	}

#endif

	printf(" * usb init\n");
	usb_init();
	usb_do_poll();

	// FIXME: Not initializing these devices here causes an interrupt storm in
	// linux.
	printf(" * sata hdd init\n");
	xenon_ata_init();

#ifndef NO_DVD
	printf(" * sata dvd init\n");
	xenon_atapi_init();
#endif

	mount_all_devices();

	/*int device_list_size = */ // findDevices();

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
	/* Stop logging and save it to first USB Device found that is writeable */
	LogDeInit();
	//extern char device_list[STD_MAX][10];

	//for (i = 0; i < device_list_size; i++)
	//{
	//	if (strncmp(device_list[i], "ud", 2) == 0)
	//	{
	//		char tmp[STD_MAX + 8];
	//		sprintf(tmp, "%sxell.log", device_list[i]);
	//		if (LogWriteFile(tmp) == 0)
	//			i = device_list_size;
	//	}
	//}

	// mount_all_devices();

	printf("\n * Looking for files...\n");
	printf(" * At any time you can insert a USB or DVD and it will be detected and searched for boot files.\n");
	printf(" * On a connected UART: 'h'=Halt, 'r'=Reboot\n\n");

	for(;;){
		if(boot_server_reachable){
			tftp_loop(boot_server_name()); // this will almost always not work
		}

		if (fallback_reachable) {
			tftp_loop(fallback_address); // Try to boot from 192.168.1.99 if no bootp server
		}

		// Look for files on any other supported/configured filesystem.
		fileloop();

		console_clrline();

		// Handle UART input
		if(kbhit()){ // If a key has been pressed
			switch(getch()){
				case 'h':
					xenon_smc_power_shutdown();
					for(;;);
					break;
				case 'r':
					xenon_smc_power_reboot();
					for(;;);
					break;
			}
		}
	}

	return 0;
}


