/*
 * xb360.c
 *
 *  Created on: Sep 4, 2008
 */

//
#include <string.h>
#include <stdio.h>
#include <time/time.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_smc/xenon_smc.h>
#include <crypt/hmac_sha1.h>
#include <crypt/rc4.h>
#include <sys/iosupport.h>
#include "xb360.h"
#include <fcntl.h>
#include "../config.h"

#define FIRST_BL_OFFSET                           0x20000000
#define FIRST_BL_SIZE                             0x8000
#define BL_KEY_PTR                                0xFE

extern int console_model;
extern char elfhdr[];
extern struct XCONFIG_SECURED_SETTINGS secured_settings;
extern void wait_and_cleanup_line();
extern char temptxt[2048];
extern char *tmptxt;

extern struct sfc sfc;

static const kventry kvlookup[] =
{
    {XEKEY_MANUFACTURING_MODE, 					0x18, 	0x01},
    {XEKEY_ALTERNATE_KEY_VAULT, 				0x19, 	0x01},
    {XEKEY_RESERVED_BYTE2, 						0x1A, 	0x01},
    {XEKEY_RESERVED_BYTE3, 						0x1B, 	0x01},
    {XEKEY_RESERVED_WORD1, 						0x1C, 	0x02},
    {XEKEY_RESERVED_WORD2, 						0x1E, 	0x02},
    {XEKEY_RESTRICTED_HVEXT_LOADER, 			0x20, 	0x02},
    {XEKEY_RESERVED_DWORD2, 					0x24, 	0x04},
    {XEKEY_RESERVED_DWORD3, 					0x28, 	0x04},
    {XEKEY_RESERVED_DWORD4, 					0x2c, 	0x04},
    {XEKEY_RESTRICTED_PRIVILEDGES, 				0x30, 	0x08},
    {XEKEY_RESERVED_QWORD2, 					0x38, 	0x08},
    {XEKEY_RESERVED_QWORD3, 					0x40, 	0x08},
    {XEKEY_RESERVED_QWORD4, 					0x48, 	0x08},
    {XEKEY_RESERVED_KEY1, 						0x50,	0x10},
    {XEKEY_RESERVED_KEY2, 						0x60, 	0x10},
    {XEKEY_RESERVED_KEY3, 						0x70, 	0x10},
    {XEKEY_RESERVED_KEY4, 						0x80, 	0x10},
    {XEKEY_RESERVED_RANDOM_KEY1, 				0x90, 	0x10},
    {XEKEY_RESERVED_RANDOM_KEY2, 				0xA0, 	0x10},
    {XEKEY_CONSOLE_SERIAL_NUMBER, 				0xB0, 	0x0C},
    {XEKEY_MOBO_SERIAL_NUMBER, 					0xBC, 	0x0C},
    {XEKEY_GAME_REGION, 						0xC8, 	0x02},
    {XEKEY_CONSOLE_OBFUSCATION_KEY, 			0xD0, 	0x10},
    {XEKEY_KEY_OBFUSCATION_KEY, 				0xE0, 	0x10},
    {XEKEY_ROAMABLE_OBFUSCATION_KEY, 			0xF0, 	0x10},
    {XEKEY_DVD_KEY, 							0x100, 	0x10},
    {XEKEY_PRIMARY_ACTIVATION_KEY, 				0x110, 	0x18},
    {XEKEY_SECONDARY_ACTIVATION_KEY, 			0x128, 	0x10},
    {XEKEY_GLOBAL_DEVICE_2DES_KEY1, 			0x138, 	0x10},
    {XEKEY_GLOBAL_DEVICE_2DES_KEY2, 			0x148, 	0x10},
    {XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY1, 	0x158, 	0x10},
    {XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY2 , 	0x168, 	0x10},
    {XEKEY_WIRED_WEBCAM_MS_2DES_KEY1, 			0x178, 	0x10},
    {XEKEY_WIRED_WEBCAM_MS_2DES_KEY2, 			0x188, 	0x10},
    {XEKEY_WIRED_CONTROLLER_MS_2DES_KEY1, 		0x198, 	0x10},
    {XEKEY_WIRED_CONTROLLER_MS_2DES_KEY2, 		0x1A8, 	0x10},
    {XEKEY_MEMORY_UNIT_MS_2DES_KEY1, 			0x1B8, 	0x10},
    {XEKEY_MEMORY_UNIT_MS_2DES_KEY2, 			0x1C8, 	0x10},
    {XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY1, 		0x1D8, 	0x10},
    {XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY2, 		0x1E8, 	0x10},
    {XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY1, 	0x1F8, 	0x10},
    {XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY2, 	0x208, 	0x10},
    {XEKEY_WIRED_WEBCAM_3P_2DES_KEY1, 			0x218, 	0x10},
    {XEKEY_WIRED_WEBCAM_3P_2DES_KEY2, 			0x228, 	0x10},
    {XEKEY_WIRED_CONTROLLER_3P_2DES_KEY1, 		0x238, 	0x10},
    {XEKEY_WIRED_CONTROLLER_3P_2DES_KEY2, 		0x248, 	0x10},
    {XEKEY_MEMORY_UNIT_3P_2DES_KEY1, 			0x258, 	0x10},
    {XEKEY_MEMORY_UNIT_3P_2DES_KEY2, 			0x268, 	0x10},
    {XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY1, 		0x278, 	0x10},
    {XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY2, 		0x288, 	0x10},
    {XEKEY_CONSOLE_PRIVATE_KEY, 				0x298, 	0x1D0},
    {XEKEY_XEIKA_PRIVATE_KEY, 					0x468, 	0x390},
    {XEKEY_CARDEA_PRIVATE_KEY, 					0x7F8, 	0x1D0},
    {XEKEY_CONSOLE_CERTIFICATE, 				0x9C8, 	0x1A8},
    {XEKEY_XEIKA_CERTIFICATE, 					0xB70, 	0x1388},
    {XEKEY_CARDEA_CERTIFICATE, 					0x1EF8, 0x2108}
};
int dumpdata(unsigned char *data,int offsetini, int sizedump)
{
    if ((sfc.initialized != SFCX_INITIALIZED)&&(console_model!=REV_CORONA2))
        return 1;
    if(console_model==REV_CORONA2)
    {
        memcpy(data, 0xC8000000+offsetini, sizedump);
    }
    else
    {
        int cntr = 0;
        int pagesnum=sizedump/0x200;
        int pageoffset=offsetini/0x200;
        while(pagesnum)
        {
            sfcx_read_page((unsigned char*) &data[cntr * 0x200], pageoffset * 0x200, 0);
            pageoffset++;
            cntr++;
            pagesnum--;
        }
    }
    return 0;
}
int savefile(unsigned char *datos,char *filename,int filelen,int modo)
{
    if (usbdrive=="") return 1;
    int fd,rc;
    char file[20];
    sprintf(file,"%s%s",usbdrive,filename);
#ifdef NO_SAVE_BIN
    if (modo==0)    return 0;
#endif
    if (modo==0)modo=0X8000;
    else modo=0X4000;
    if ((fd = open(file, O_CREAT |O_WRONLY|modo )) != -1)
    {
        if (rc=write(fd, datos, filelen)==-1)
            printlog("Error while saving");
        close(fd);
        return 0;
    }
}
void print_key(char *name, unsigned char *data)
{
    int i=0;
    sprintf(tmptxt," * %s         :",name);
    printlog(temptxt);
    for(i=0; i<16; i++)
    {
        tmptxt += sprintf(tmptxt,"%02X",data[i]);
    }
    printlog(temptxt);
    sprintf(name,"%s.txt",name);
    savefile(temptxt,name,0x20,1);

    printlog("\n");
}
void print_key_plain(char *name, unsigned char *data,int size)
{
    int i=0;
    printlog(name);
    for(i=0; i<size; i++)
    {
        tmptxt += sprintf(tmptxt,"%c",data[i]);
    }
    printlog(temptxt);
    printlog("\n");
}
int decrypt_cpu(unsigned char*data , unsigned char*cpu_key,int filesize)
{
    unsigned char hmac_key[0x10];
    memcpy(hmac_key, data, 0x10);
    //print_key("kv_read: hmac key", hmac_key);
    unsigned char rc4_key[0x10];
    memset(rc4_key, 0, 0x10);
    HMAC_SHA1(cpu_key, hmac_key, rc4_key, 0x10);
    //print_key("kv_read: rc4 key", rc4_key);
    unsigned char rc4_state[0x100];
    memset(rc4_state, 0, 0x100);

    rc4_init(rc4_state, rc4_key ,0x10);
    rc4_crypt(rc4_state, (unsigned char*) &data[0x10], filesize - 0x10);
    return 0;
}
int decrypt_smc(unsigned char *data)
{
    unsigned char b,key[] = {0x42, 0x75, 0x4e, 0x79};

    unsigned short mod;
    int j;
    for(j=0; j<0x3000; j++)
    {
        b = data[j];
        mod = b * 0xFB;
        data[j] = (b ^ (key[j&3] & 0xFF));
        key[(j+1)&3] += mod;
        key[(j+2)&3] += mod >> 8;
    }
    return 0;

}
int cpu_get_key(unsigned char *data)
{
    *(unsigned long long*)&data[0] = xenon_secotp_read_line(3);
    *(unsigned long long*)&data[8] = xenon_secotp_read_line(5);
    return 0;
}
int virtualfuses_read(unsigned char *data)
{
    int vfuses_offset = 0x95000; //Page (0x4A8 * 0x200)
    if ((sfc.initialized != SFCX_INITIALIZED)&&(console_model!=REV_CORONA2)) return 1;
//    if (sfc.initialized != SFCX_INITIALIZED) return 1;

    int status = dumpdata(data, vfuses_offset, 0x200);

    //read from nand
    if (!SFCX_SUCCESS(status))
        return 2; //failure
    else
        return 0; //success
}
int get_virtual_cpukey(unsigned char *data)
{
    int result = 0;
    unsigned char buffer[MAX_PAGE_SZ];
    result = virtualfuses_read(buffer);

    if (result!=0)
    {
        printlog(" ! SFCX error while reading virtualfuses\n");
        return result;
    }

    //if we got here then it was at least able to read from nand
    //now we need to verify the data somehow
    if (buffer[0]==0xC0 && buffer[1]==0xFF && buffer[2]==0xFF && buffer[3]==0xFF)
    {
        memcpy(data,&buffer[0x20],0x10);
        return 0;
    }
    else
        /* No Virtual Fuses were found at 0x95000*/
        return 1;
}
int kv_get_key(unsigned char keyid, unsigned char *keybuf, int *keybuflen, unsigned char *keyvault)//read key from keyvault
{
    if (keyid > 0x38)
        return 1;

//    if (*keybuflen != kvlookup[keyid].length)
//    {
//        *keybuflen = kvlookup[keyid].length;
//        return 2;
//    }
    memcpy(keybuf, keyvault + kvlookup[keyid].offset, kvlookup[keyid].length);

    return 0;
}
int kv_read(unsigned char *data, int virtualcpukey)
{

    if ((sfc.initialized != SFCX_INITIALIZED)&&(console_model!=REV_CORONA2))
        return 1;

    unsigned char cpu_key[0x10];
    if (virtualcpukey)
        get_virtual_cpukey(cpu_key);
    else
        cpu_get_key(cpu_key);

    int kv_offset = 0;

    unsigned char temp[0x200];
    dumpdata(temp,0,0x200);

    kv_offset=getmem(temp,KV_FLASH_PTR,4);
    dumpdata(data,kv_offset,KV_FLASH_SIZE);
    savefile(data,"kv_enc.bin",KV_FLASH_SIZE,0);
    decrypt_cpu(data,cpu_key,KV_FLASH_SIZE);
    savefile(data,"kv.bin",KV_FLASH_SIZE,0);

    //Now then do a little check to make sure it is somewhat correct
    //We check the hmac_sha1 of the data and compare that to
    //the hmac_sha1 key in the header of the keyvault
    //basically the reverse of what we did to generate the key for rc4
    unsigned char data2[KV_FLASH_SIZE];
    unsigned char out[20];
    unsigned char tmp[] = {0x07, 0x12};
    HMAC_SHA1_CTX ctx;

    //the hmac_sha1 seems destructive
    //so we make a copy of the data
    memcpy(data2, data, KV_FLASH_SIZE);

    HMAC_SHA1_Init(&ctx);
    HMAC_SHA1_UpdateKey(&ctx, (unsigned char *) cpu_key, 0x10);
    HMAC_SHA1_EndKey(&ctx);

    HMAC_SHA1_StartMessage(&ctx);

    HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*) &data2[0x10], KV_FLASH_SIZE - 0x10);
    HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*)   &tmp[0x00], 0x02);	//Special appendage

    HMAC_SHA1_EndMessage(out, &ctx);
    HMAC_SHA1_Done(&ctx);

    int index = 0;
    while (index < 0x10)
    {
        if (data[index] != out[index])
        {
            // Hmm something is wrong, hmac is not matching
            //printf("\n ! kv_read: kv hash check failed\n");
            return 2;
        }
        index += 1;
    }

    return 0;
}
int kv_get_dvd_key(unsigned char *dvd_key)
{
    unsigned char data[KV_FLASH_SIZE], tmp[0x10],serial[0x10];
    int result = 0;
    int keylen = 0x10;

    result = kv_read(data, 0);
    if (result == 2 && get_virtual_cpukey(tmp) == 0)
    {
        printlog("! Attempting to decrypt DVDKey with Virtual CPU Key !\n");
        result = kv_read(data, 1);
    }
    if (result != 0)
    {
        printlog(" ! kv_get_dvd_key Failure: kv_read\n");
        if (result == 2)  //Hash failure
        {
            printlog(" !   the hash check failed probably as a result of decryption failure\n");
            printlog(" !   make sure that the CORRECT key vault for this console is in flash\n");
            printlog(" !   the key vault should be at offset 0x4200 for a length of 0x4200\n");
            printlog(" !   in the 'raw' flash binary from THIS console\n");
        }
        return 1;
    }

    result = kv_get_key(XEKEY_DVD_KEY, dvd_key, kvlookup[XEKEY_DVD_KEY].length, data);
    if (result == 0) print_key("DvdKey", dvd_key);
    else
    {
        tmptxt += sprintf(tmptxt," ! kv_get_dvd_key Failure: kv_get_key %d\n", result);
        printlog(temptxt);
        return result;
    }
    result = kv_get_key(XEKEY_CONSOLE_SERIAL_NUMBER, serial, kvlookup[XEKEY_CONSOLE_SERIAL_NUMBER].length, data);
    if (result == 0) print_key_plain (" * Serial Number  :", serial,kvlookup[XEKEY_CONSOLE_SERIAL_NUMBER].length);
    else
    {
        tmptxt += sprintf(tmptxt," ! Get Serial Failure: %d\n", result);
        printlog(temptxt);
        return result;
    }
    return 0;
}
void print_cpu_key(void)
{
    unsigned char key[0x10];

    memset(key, '\0', sizeof(key));

    if (cpu_get_key(key)==0)
        print_key("CpuKey", key);
    memset(key, '\0',sizeof(key));
    if (get_virtual_cpukey(key)==0)
        print_key("Virtual-Cpukey", key);
}
void print_dvd_keys(void)
{
    unsigned char key[0x10];

    memset(key, '\0', sizeof(key));
    kv_get_dvd_key(key);
    printf("\n");
}
void nand_data(void)
{
    int bl_offset,bl_size;
    unsigned char * data;
    data = (unsigned char *) malloc(0x100000);
    unsigned char *bl;
    char name[20];
    int build=0;

    dumpdata(data,0,0x100000);
    savefile(data,"data.bin",0x100000,0);

    bl_offset=getmem(data,SMC_PTR_OFF,4);
    bl_size = getmem(data,SMC_PTR_SIZE,4);
    bl=data+bl_offset;
    savefile(bl,"smc_enc.bin",bl_size,0);
    decrypt_smc(bl);
    savefile(bl,"smc.bin",bl_size,0);
    tmptxt += sprintf(tmptxt," * SMC %d.%d",bl[0x101],bl[0x102]);

    bl_offset=getmem(data,CBA_PTR_OFF,4);
    int cf=0;
    int cbb=0;
    while (1)
    {
        bl_size=getmem(data,bl_offset+0x0c,4);
        bl=data+bl_offset;
        if (bl[0]!=0x43)//CB CD CE
        {
            if (cf==0)
            {
                cf=1;
                bl_offset =getmem(data,0x0c,4);
                continue;
            }
            else
            {
                break;
            }
        }

        build = bl[2];
        build <<= 8;
        build |= bl[3];

        if (bl[1]==0x42)
        {
            if (cbb==0)
            {
                sprintf(name,"%c%cA_%d.bin",bl[0],bl[1],build);
                tmptxt += sprintf(tmptxt," * %c%cA %d",bl[0],bl[1],build);
                cbb=1;
            }
            else
            {
                sprintf(name,"%c%cB_%d.bin",bl[0],bl[1],build);
                tmptxt += sprintf(tmptxt," * %c%cB %d",bl[0],bl[1],build);
            }
        }
        else
        {
            sprintf(name,"%c%c_%d.bin",bl[0],bl[1],build);
            tmptxt += sprintf(tmptxt," * %c%c %d",bl[0],bl[1],build);
        }

        printlog(temptxt);
        savefile(bl,name,bl_size,0);
        bl_offset+=bl_size;
    }
    printf("\n");
    free(data);
    return 0;
}
int getmem(unsigned char *data,int off,int num)
{
    int mem=0;

    int offset=off;
    mem =  data[offset];
    int x=1;
    for (x=1; x<num; x++)
    {
        mem <<= 8;
        mem |= data[offset+x];
    }
    return mem;
}
int updateXeLL(void * addr, unsigned len)
{
    int i, j, k, status, startblock, current, offsetinblock, blockcnt;
    unsigned char *user, *spare;

    if (sfc.initialized != SFCX_INITIALIZED)
    {
        printlog(" ! sfcx is not initialized! Unable to update XeLL in NAND!\n");
        return -1;
    }

    printlog("\n * found XeLL update. press power NOW if you don't want to update.\n");
    delay(15);

    for (k = 0; k < XELL_OFFSET_COUNT; k++)
    {
        current = xelloffsets[k];
        offsetinblock = current % sfc.block_sz;
        startblock = current/sfc.block_sz;
        blockcnt = offsetinblock ? (XELL_SIZE/sfc.block_sz)+1 : (XELL_SIZE/sfc.block_sz);


        spare = (unsigned char*)malloc(blockcnt*sfc.pages_in_block*sfc.meta_sz);
        if(!spare)
        {
            printlog(" ! Error while memallocating filebuffer (spare)\n");
            return -1;
        }
        user = (unsigned char*)malloc(blockcnt*sfc.block_sz);
        if(!user)
        {
            printlog(" ! Error while memallocating filebuffer (user)\n");
            return -1;
        }
        j = 0;
        unsigned char pagebuf[MAX_PAGE_SZ];

        for (i = (startblock*sfc.pages_in_block); i< (startblock+blockcnt)*sfc.pages_in_block; i++)
        {
            sfcx_read_page(pagebuf, (i*sfc.page_sz), 1);
            //Split rawpage into user & spare
            memcpy(&user[j*sfc.page_sz],pagebuf,sfc.page_sz);
            memcpy(&spare[j*sfc.meta_sz],&pagebuf[sfc.page_sz],sfc.meta_sz);
            j++;
        }

        if (memcmp(&user[offsetinblock+(XELL_FOOTER_OFFSET)],XELL_FOOTER,XELL_FOOTER_LENGTH) == 0)
        {
            printf(" * XeLL Binary in NAND found @ 0x%08X\n", (startblock*sfc.block_sz)+offsetinblock);

            memcpy(&user[offsetinblock], addr,len); //Copy over updxell.bin
            printlog(" * Writing to NAND!\n");
            j = 0;
            for (i = startblock*sfc.pages_in_block; i < (startblock+blockcnt)*sfc.pages_in_block; i ++)
            {
                if (!(i%sfc.pages_in_block))
                    sfcx_erase_block(i*sfc.page_sz);
                /* Copy user & spare data together in a single rawpage */
                memcpy(pagebuf,&user[j*sfc.page_sz],sfc.page_sz);
                memcpy(&pagebuf[sfc.page_sz],&spare[j*sfc.meta_sz],sfc.meta_sz);
                j++;

                if (!(sfcx_is_pageerased(pagebuf))) // We dont need to write to erased pages
                {
                    memset(&pagebuf[sfc.page_sz+0x0C],0x0, 4); //zero only EDC bytes
                    sfcx_calcecc((unsigned int *)pagebuf); 	  //recalc EDC bytes
                    sfcx_write_page(pagebuf, i*sfc.page_sz);
                }
            }
            printlog(" * XeLL flashed! Reboot the xbox to enjoy the new build\n");
//            delay(5);
//            xenon_smc_power_reboot();
//            return 0;
            for(;;);

        }
    }
    printlog(" ! Couldn't locate XeLL binary in NAND. Aborting!\n");
    return -1;
}
int save_cpu_key()
{
    int i;
    unsigned char key[0x10];
    unsigned char *user, *spare;
    unsigned char pagebuf[MAX_PAGE_SZ];

    if (sfc.initialized != SFCX_INITIALIZED)
    {
        printlog(" ! sfcx is not initialized! Unable to save CPU Key to NAND!\n");
        return -1;
    }

    printlog("\n * Backing Up CPU Key.\n");

    spare = (unsigned char*)malloc(sfc.pages_in_block*sfc.meta_sz);
    if(!spare)
    {
        printlog(" ! Error while memallocating filebuffer (spare)\n");
        return -1;
    }

    user = (unsigned char*)malloc(sfc.block_sz);
    if(!user)
    {
        printlog(" ! Error while memallocating filebuffer (user)\n");
        return -1;
    }


    for (i = 0; i < 1 * sfc.pages_in_block; i++)
    {
        sfcx_read_page(pagebuf, (i*sfc.page_sz), 1);
        memcpy(&user[i*sfc.page_sz],  pagebuf,             sfc.page_sz);
        memcpy(&spare[i*sfc.meta_sz],&pagebuf[sfc.page_sz],sfc.meta_sz);
    }

    memset(key, '\0', sizeof(key));
    cpu_get_key(key);

    /* Don't keep copying the key, it wears out the flash :s */
    if(memcmp(key, &user[0x100], 0x10) == 0)
    {
        printlog(" * Key already present \n");
        return 0;
    }

    memcpy(&user[0x100],key,0x10);

    sfcx_erase_block(0);

    for (i = 0; i < sfc.pages_in_block; i ++)
    {

        /* Copy user & spare data together in a single rawpage */
        memcpy(pagebuf,              &user[i*sfc.page_sz], sfc.page_sz);
        memcpy(&pagebuf[sfc.page_sz],&spare[i*sfc.meta_sz],sfc.meta_sz);

        memset(&pagebuf[sfc.page_sz+0x0C],0x0, 4); //zero only EDC bytes
        sfcx_calcecc((unsigned int *)pagebuf); 	  //recalc EDC bytes
        sfcx_write_page(pagebuf, i*sfc.page_sz);
    }

    printlog(" * Key copied \n");
    return 0;
}
int writelog(char *logtext)
{
    if (usbdrive!="")
    {
        char filename[13];
        sprintf(filename,"%sLOG.txt",usbdrive);
        int fd;
        if ((fd = open(filename, O_CREAT|O_APPEND |O_WRONLY)) != -1)
            write(fd, logtext, strlen(logtext));
        close(fd);
    }
    tmptxt=&temptxt;
    return 1;

}
int logusb()//detect if usb device
{
    static char device_list[STD_MAX][10];
    int i;
    usb_do_poll();
    for (i = 3; i < STD_MAX; i++)
    {
        if (devoptab_list[i]->structSize && devoptab_list[i]->name[0]=='u')
        {
            sprintf(device_list[0], "%s:/", devoptab_list[i]->name);
            usbdrive = device_list[0];
            char filename[]="%sLOG.txt",usbdrive;
            return 0;
        }
    }
    usbdrive="";
    return 1;
}


int dumpbl()
{
    unsigned char * buf;
    buf = (unsigned char *) malloc(FIRST_BL_SIZE);
    int offset=0;

    memcpy(buf, FIRST_BL_OFFSET, FIRST_BL_SIZE);
    savefile(buf,"1BL.bin",FIRST_BL_SIZE,0);

    offset =  buf[BL_KEY_PTR];
    offset <<= 8;
    offset |= buf[BL_KEY_PTR+1];
    offset+=0x148;

    tmptxt += sprintf(tmptxt,"   1BL found at offset 0x%08X\n\n",offset );
    print_key("1BLKey",buf+offset);

    free(buf);
    return 0;
}


uint32_t xenon_get_PCIBridgeRevisionID()
{
    return ((read32(0xd0000008) << 24) >> 24);
}
unsigned int xenon_get_console_type()
{
    unsigned int PVR, consoleVersion, tmp;
    uint32_t DVEversion, PCIBridgeRevisionID;

    PCIBridgeRevisionID = xenon_get_PCIBridgeRevisionID();
    tmptxt += sprintf(tmptxt,"\n* PCIBridgeRevisionID:%d\n",PCIBridgeRevisionID);
    consoleVersion = (read32(0xd0010000) >> 16) & 0xFFFF;

    xenon_smc_ana_read(0xfe, &DVEversion);
    tmp = DVEversion;
    tmp = (tmp & ~0xF0) | ((DVEversion >> 12) & 0xF0);
    DVEversion = tmp & 0xFF;
    tmptxt += sprintf(tmptxt,"\n* DVEversion:%d\n",DVEversion);

asm volatile("mfpvr %0" : "=r" (PVR));

    if(PVR == 0x710200 || PVR == 0x710300)
    {
        return REV_ZEPHYR;
    }
    else
    {
        if(consoleVersion < 0x5821)
        {
            tmptxt += sprintf(tmptxt,"Model Xenon Detected\n");
            return REV_XENON;
        }
        else if(consoleVersion >= 0x5821 && consoleVersion < 0x5831)
        {
            tmptxt += sprintf(tmptxt,"Model Falcon Detected\n");
            return REV_FALCON;
        }
        else if(consoleVersion >= 0x5831 && consoleVersion < 0x5841)
        {
            tmptxt += sprintf(tmptxt,"Model Jasper Detected\n");
            return REV_JASPER;
        }
        else if(consoleVersion >= 0x5841 && consoleVersion < 0x5851)
        {
//            if (sfc.initialized == SFCX_INITIALIZED)
            if (sfcx_readreg(SFCX_PHISON) != 0)
            {
                tmptxt += sprintf(tmptxt,"Model Corona v2 detected\n");
                return REV_CORONA2;
            }

            else
            {
                if (DVEversion >= 0x20)
                {
                    tmptxt += sprintf(tmptxt,"Model Corona v1 detected\n");
                    return REV_CORONA;
                }
                else
                {
                    tmptxt += sprintf(tmptxt,"Model Trinity detected\n");
                    return REV_TRINITY;
                }
            }
        }
        else if(consoleVersion >= 0x5851)
        {
            tmptxt += sprintf(tmptxt,"Model Winchester Detected\n");
            return REV_WINCHESTER;
        }
    }

    return -1;
}
