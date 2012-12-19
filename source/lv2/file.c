/*
used for zlib support ...
*/

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <zlib.h>
#include <xetypes.h>
#include <elf/elf.h>
#include <network/network.h>
#include <console/console.h>
#include <sys/iosupport.h>
#include <ppc/timebase.h>
#include <xenon_nand/xenon_sfcx.h>

#include "config.h"
#include "file.h"
#include "xb360/xb360.h"
#include "kboot/kbootconf.h"
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#define CHUNK 16384
#define	O_BINARY	0x8000

unsigned char* blockbuf;
extern int console_model;
extern char dt_blob_start[];
extern char dt_blob_end[];

const unsigned char elfhdr[] = {0x7f, 'E', 'L', 'F'};
const unsigned char cpiohdr[] = {0x30, 0x37, 0x30, 0x37};
const unsigned char kboothdr[] = "#KBOOTCONFIG";

struct filenames filelist[] =
{
    {"readnanddump",TYPE_READTXT},
    {"updxell.bin",TYPE_UPDXELL},
    {"nandflash.bin",TYPE_NANDIMAGE},
    {"updflash.bin",TYPE_NANDIMAGE},
    {"xenon.z",TYPE_ELF},
    {"vmlinux",TYPE_ELF},
//    {"xenon.elf",TYPE_ELF},
    {"kboot.conf",TYPE_KBOOT},
    {NULL,NULL}
};
//Decompress a gzip file ...
int inflate_read(char *source,int len,char **dest,int * destsize, int gzip)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char out[CHUNK];
    int totalsize = 0;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;

    if(gzip)
        ret = inflateInit2(&strm, 16+MAX_WBITS);
    else
        ret = inflateInit(&strm);

    if (ret != Z_OK)
        return ret;

    strm.avail_in = len;
    strm.next_in = (Bytef*)source;

    /* run inflate() on input until output buffer not full */
    do
    {
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)out;
        ret = inflate(&strm, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        switch (ret)
        {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            inflateEnd(&strm);
            return ret;
        }
        have = CHUNK - strm.avail_out;
        totalsize += have;
        if (totalsize > ELF_MAXSIZE)
            return Z_BUF_ERROR;
        //*dest = (char*)realloc(*dest,totalsize);
        memcpy(*dest + totalsize - have,out,have);
        *destsize = totalsize;
    }
    while (strm.avail_out == 0);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void wait_and_cleanup_line()
{
    unsigned int w=0;
    console_get_dimensions(&w,NULL);

    char sp[w];

    memset(sp,' ',w);
    sp[w-1]='\0';

    uint64_t t=mftb();
    while(tb_diff_msec(mftb(),t)<200)  // yield to network
    {
        network_poll();
    }

    printf("\r%s\r",sp);
}

void launch_file(void * addr, unsigned len, int filetype)
{

    switch (filetype)
    {

    case TYPE_ELF:
        if (memcmp(addr, elfhdr, 4))
            return;
        printlog(" * Launching ELF...\n");
        //check if addr point to a gzip file
        unsigned char * gzip_file = (unsigned char *)addr;
        if((gzip_file[0]==0x1F)&&(gzip_file[1]==0x8B))
        {
            //found a gzip file
            printlog(" * Found a gzip file...\n");
            char * dest = malloc(ELF_MAXSIZE);
            int destsize = 0;
            if(inflate_read((char*)addr, len, &dest, &destsize, 1) == 0)
            {
                //relocate elf ...
                memcpy(addr,dest,destsize);
                printlog(" * Successfully unpacked...\n");
                free(dest);
                len=destsize;
            }
            else
            {
                printlog(" * Unpacking failed...\n");
                free(dest);
                return;
            }
        }
//        usb_do_poll();
        elf_runWithDeviceTree(addr,len,dt_blob_start,dt_blob_end-dt_blob_start);
        break;
    case TYPE_INITRD:
        printlog(" * Loading initrd into memory ...\n");
        kernel_prepare_initrd(addr,len);
        break;
    case TYPE_KBOOT:
        printlog(" * Loading kboot.conf ...\n");
        try_kbootconf(addr,len);
        break;
    case TYPE_UPDXELL:
        if (memcmp(addr + XELL_FOOTER_OFFSET, XELL_FOOTER, XELL_FOOTER_LENGTH) || len != XELL_SIZE)
            return;
        printlog(" * Loading UpdXeLL binary...\n");
        updateXeLL(addr,len);
        break;
    default:
        printlog("! Unsupported filetype supplied!\n");
    }
}

int try_load_file(char *filename, int filetype)
{
    printf("Trying to Load %s, type %d  \r",filename,filetype);
    int f = open(filename, O_RDONLY);

    if (f < 0)
    {
        printf("Error .     \r",filename);
        return f;
    }
    printf("Success¡¡¡      \r",filename);

    if(filetype == TYPE_READTXT)
    {
        printlog("Read flag file detected\n");
        if(console_model==REV_CORONA2)
        {
            unsigned char * data;
            data = (unsigned char *) malloc(0x3000000);
            dumpdata(data,0,0x3000000);
            savefile(data,"nanddump.bin",0x3000000,0);}
        else rawread(filename);
        return 0;
    }
	if(filetype == TYPE_NANDIMAGE){
        printlog("Nand file detected\n");
        try_rawflash(filename);
        delay(15);
        xenon_smc_power_reboot();

	}

    wait_and_cleanup_line();
    printf("Trying %s...",filename);

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

    launch_file(buf,r,filetype);

    free(buf);
    return 0;
}

void fileloop() {
        char filepath[255];

        int i,j=0;
        usb_do_poll();
        for (i = 3; i < 16; i++) {

                if (devoptab_list[i]->structSize) {
                        do{
                           sprintf(filepath, "%s:/%s", devoptab_list[i]->name,filelist[j].filename);
                           try_load_file(filepath,filelist[j].filetype);
                           j++;
                           usb_do_poll();
                        } while(filelist[j].filename != NULL);
                        j = 0;
                }
        }
}


void tftp_loop()
{
    int i=0;
    network_poll();
    do
    {
        wait_and_cleanup_line();
        printf("Trying TFTP %s:%s...            \r",boot_server_name(),filelist[i].filename);
        boot_tftp(boot_server_name(), filelist[i].filename, filelist[i].filetype);
        i++;
        network_poll();
        usb_do_poll();
    }
    while(filelist[i].filename != NULL);
    wait_and_cleanup_line();
    printf("Trying TFTP %s:%s...             \r",boot_server_name(),boot_file_name());
    /* Assume that bootfile delivered via DHCP is an ELF */
    boot_tftp(boot_server_name(),boot_file_name(),TYPE_ELF);
}
int loadImage(char *filename)
{

    struct stat s;
    int size=sfc.size_bytes_phys;
    int fd,rc,addr,status;
    int i=0;
    int readsz = sfc.pages_in_block*sfc.page_sz_phys;
    int numblocks = (size/sfc.block_sz_phys);
    if (numblocks==0x1000)numblocks=0x200;
    blockbuf = malloc(readsz);

    if ((fd = open(filename, O_CREAT |O_WRONLY|O_BINARY )) != -1)
    {
        fprintf(stderr, "Creating file %s\n",filename);
        while(i <numblocks)
        {
            printf("Reading block 0x%04x of 0x%04x    \r", i+1, numblocks);
            addr = i*sfc.block_sz;
            status=sfcx_read_block(blockbuf,addr, 1);

            if((status & (STATUS_BB_ER|STATUS_ECC_ER))!= 0)
                printf("block 0x%x seems bad, status 0x%08x\n", i, status);

            if (rc=write(fd, blockbuf, readsz)==-1)

                printf("Error while reading block 0x%x\n", i);
            i++;
        }
        printf("\nDone. 0x%x blocks Readed from Nand to %s\nConsole will Turn Off now.",i,filename);
        delay(5);
        xenon_smc_power_shutdown();
    }
    else
        fprintf(stderr, "Error opening the file %s\n",filename);

    close(fd);
    return 0;
}

int rawread (char *filename)
{
	if (sfc.initialized != SFCX_INITIALIZED)
	{
	    if (console_model==REV_CORONA2){
            unsigned char * b;
            b = (unsigned char *) malloc(0x480000);
            memcpy(b, 0x20000000, 0x480000);//0x20000000
            savefile(b,"nanddump.bin",0x480000,0);
            free(b);
            }
	    else{
		printlog("NAND init failed! <STOP>\n");
		return 1;}
	}
    printlog("initializing usb\n");
//    usb_init();
    usb_do_poll();
    loadImage("uda0:/nanddump.bin");
    if (remove(filename) == -1)
        perror("Error in deleting a file");
    delay(5);

    return 0;
}
