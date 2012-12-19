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
#include <stdarg.h>

int console_model=0;
char FUSES[350]; /* this string stores the ascii dump of the fuses */
char temptxt[2048];
char *tmptxt=temptxt;

unsigned char stacks[6][0x10000];

void do_asciiart()
{
    char *p = asciiart;
    while (*p)
        console_putch(*p++);
}
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
    for(i=1; i<6; ++i)
    {
        xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
        while(xenon_is_thread_task_running(i));
    }
    reset_timebase_task(); // don't forget thread 0
    std(0x200611a0,0x1ff); // restart timebase
}
void printlog(char * textlog, ...)
{
    printf(textlog);
    writelog(textlog);
}
#define PATCH_VERSION "1.025"
int main()
{
    int i;

    // linux needs this
    synchronize_timebases();
    // irqs preinit (SMC related)
    *(volatile uint32_t*)0xea00106c = 0x1000000;
    *(volatile uint32_t*)0xea001064 = 0x10;
    *(volatile uint32_t*)0xea00105c = 0xc000000;

    xenon_smc_start_bootanim();
    xenon_smc_set_power_led(0, 0, 1);

    tmptxt += sprintf(tmptxt,"\n\n"
             "******************************************************************************\n"
             "**************************    NEW LOG  Patch v(" PATCH_VERSION ")    *********************\n"
             "******************************************************************************\n");

    console_model= xenon_get_console_type();

    if (console_model<5) //if not Corona Init Video
    {
        setbuf(stdout,NULL);
        xenos_init(VIDEO_MODE_AUTO);
        console_set_colors(0X00300000,0Xffffffff); // ); THEME
        console_init();
        do_asciiart();
        printf("\nXeLL - Xenon linux loader second stage " LONGVERSION " Patch v(" PATCH_VERSION ") \n\n");
    }
    usb_init();
    usb_do_poll();
    mount_all_devices();
    logusb();
    writelog(temptxt);
    sprintf(tmptxt,"\n* CPU PVR: %08x \n", mfspr(287));
    printlog(temptxt);

    xenon_sound_init();

    printlog("\n * nand init\n");
    xenon_config_init();

    printlog(" * network init\n");
    network_init();

    printlog(" * starting httpd server...");
    httpd_start();
    printlog("success\n");

    printlog(" * sata hdd init\n");
    xenon_ata_init();

    printlog(" * sata dvd init\n");
    xenon_atapi_init();

    findDevices();
    printlog(temptxt);

    save_cpu_key();//save Cpukey to Nand (0x100 )

    sfcx_init();

#ifndef NO_PRINT_CONFIG
    if (sfc.initialized == SFCX_INITIALIZED)//if Sfcx show Nand Info
    {
        tmptxt += sprintf(tmptxt,"\n * Nand INFO:\n");
        tmptxt += sprintf(tmptxt,"   config register     = %08X\n", sfc.config);
        tmptxt += sprintf(tmptxt,"   sfc:page_sz         = %08X\n", sfc.page_sz);
        tmptxt += sprintf(tmptxt,"   sfc:meta_sz         = %08X\n", sfc.meta_sz);
        tmptxt += sprintf(tmptxt,"   sfc:page_sz_phys    = %08X\n", sfc.page_sz_phys);
        tmptxt += sprintf(tmptxt,"   sfc:pages_in_block  = %08X\n", sfc.pages_in_block);
        tmptxt += sprintf(tmptxt,"   sfc:block_sz        = %08X\n", sfc.block_sz);
        tmptxt += sprintf(tmptxt,"   sfc:block_sz_phys   = %08X\n", sfc.block_sz_phys);
        tmptxt += sprintf(tmptxt,"   sfc:size_mb         = %dMB\n", sfc.size_mb);
        tmptxt += sprintf(tmptxt,"   sfc:size_bytes      = %08X\n", sfc.size_bytes);
        tmptxt += sprintf(tmptxt,"   sfc:size_bytes_phys = %08X\n", sfc.size_bytes_phys);
        tmptxt += sprintf(tmptxt,"   sfc:size_pages      = %08X\n", sfc.size_pages);
        tmptxt += sprintf(tmptxt,"   sfc:size_blocks     = %08X\n", sfc.size_blocks);
        printlog(temptxt);
    }
    else
    {
        printlog(" ! sfcx initialization failure\n");
        printlog(" ! nand related features will not be available\n");
    }

    printlog("\n * FUSES - write them down and keep them safe:\n\n");

    unsigned int ldv=0;
    for (i=0; i<12; ++i)//Show Fuses
    {
        u64 line;
        unsigned int hi,lo;
        unsigned int tmp;

        line=xenon_secotp_read_line(i);
        hi=line>>32;
        lo=line&0xffffffff;
        if ((i>6)&&(i<9))
        {
            tmp=hi;
            while(tmp!=0)
            {
                if (tmp!=0)ldv++;
                tmp=tmp<<4;
            }
            tmp=lo;
            while(tmp!=0)
            {
                if (tmp!=0)ldv++;
                tmp=tmp<<4;
            }
        }
        tmptxt += sprintf(tmptxt,"   fuseset %02d: %08X%08X\n", i, hi, lo);
    }
    printlog(temptxt);
    tmptxt += sprintf(tmptxt,"\n   LDV: %d\n",ldv);
    printlog(temptxt);
#endif

    dumpbl();

    print_cpu_key();
    if ((sfc.initialized == SFCX_INITIALIZED)||(console_model==REV_CORONA2))
    {
        print_dvd_keys();

#ifndef NO_PRINT_CONFIG
        nand_data();//if Sfcx show KV Info
        printf("\n");
#endif
    }

    network_print_config();

    if (console_model==6) xenon_smc_power_shutdown(); //CORONAV2
    if ((console_model>4) && (usbdrive=="")) xenon_smc_power_shutdown();//if Corona and no Usb Turn Off

    printlog("\n * Looking for files on local media and TFTP...\n");
    for(;;)
    {
        fileloop();
        tftp_loop();
        delay(1);
    }
    return 0;
}
