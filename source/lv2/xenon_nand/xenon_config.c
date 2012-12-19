/*
 * xenon_config.c
 *
 *  Created on: Mar 4, 2011
 */

#include <xetypes.h>
#include <stdio.h>
#include <string.h>
#include "xenon_config.h"
#include "xenon_sfcx.h"

#define BLOCK_OFFSET 3 //We want the 3rd block of the 4 config blocks)

extern struct sfc sfc;
struct XCONFIG_SECURED_SETTINGS secured_settings = {0};

static int xenon_config_initialized=0;
unsigned char pagebuf[MAX_PAGE_SZ];   //Max known hardware physical page size
extern char temptxt[2048];
extern char *tmptxt;
void xenon_config_init(void)
{
	if (xenon_config_initialized) return;

	sfcx_init();

	// depends on sfcx already being initialized
	if (sfc.initialized != SFCX_INITIALIZED)
	{
		//TODO: wassup with this...
		tmptxt += sprintf(tmptxt," ! config: sfcx not initialized\n");
	}
	else
	{
		//calc our address (specific for our one structure)
		int addr = sfc.addr_config + (BLOCK_OFFSET * sfc.block_sz) + sfc.page_sz;
		int status = sfcx_read_page(pagebuf, addr, 0);
		savefile(pagebuf-0xc200,"smc_config.bin",0x1000,0);

		//read from nand
		if (SFCX_SUCCESS(status)){
			//TODO: check if we got erased or zeroed nand data
			memcpy(&secured_settings, &pagebuf[0x0], sizeof secured_settings);
			xenon_config_initialized=1;
            tmptxt += sprintf(tmptxt," sfcx initialized OK\n");
		}
	}
}

int xenon_config_get_avregion(void)
{
	unsigned char buf[0x4] = {0x00,0x00,0x00,0x00};

	//read from nand
	memcpy(buf, &secured_settings.AVRegion, 4);

	//check if we got erased nand data
	if (buf[0x0]==0xFF && buf[0x1]==0xFF && buf[0x2]==0xFF)
		return 0;

	//check if we got zeroed nand data
	if (buf[0x0]==0x00 && buf[0x1]==0x00 && buf[0x2]==0x00)
		return 0;



	if (buf[0x2] >= 0x01 && buf[0x2] <= 0x04)
		return buf[0x2];
	else
	{
		tmptxt += sprintf(tmptxt," !!! Unknown AVRegion: %02X!!!\n",buf[0x2]);
		return buf[0x2];
	}
}

void xenon_config_get_mac_addr(unsigned char *hwaddr)
{
	unsigned char dmac[0x6] = {0x00,0x22,0x48,0xFF,0xFF,0xFF};

	//read from nand
	memcpy(hwaddr, &secured_settings.MACAddress[0x0], 6);

	//check if we got erased nand data
	if (hwaddr[0x0]==0xFF && hwaddr[0x1]==0xFF && hwaddr[0x2]==0xFF)
		memcpy(hwaddr, dmac, 6);

	//check if we got zeroed nand data
	if (hwaddr[0x0]==0x00 && hwaddr[0x1]==0x00 && hwaddr[0x2]==0x00)
		memcpy(hwaddr, dmac, 6);

	//tmptxt += sprintf(tmptxt,"NIC MAC set to %02X%02X%02X%02X%02X%02X\n",
	//		hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
}

int xenon_config_get_vid_delta()
{
	u8 delta=secured_settings.PowerMode.VIDDelta;

	//check if we got erased or zeroed nand data
	if (delta==0 || delta==0xff) return -1;

	return delta;
}
