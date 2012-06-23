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

#include "xb360/xb360.h"
#include "config.h"
#include "kboot/kbootconf.h"

#define CHUNK 16384

//int i;

extern char dt_blob_start[];
extern char dt_blob_end[];

const unsigned char elfhdr[] = {0x7f, 'E', 'L', 'F'};
const unsigned char cpiohdr[] = {0x30, 0x37, 0x30, 0x37};
const unsigned char kboothdr[] = "#KBOOTCONFIG";
const unsigned char *filenames[] = {"updxell.bin","kboot.conf","xenon.elf","xenon.z","vmlinux", NULL};

//Decompress a gzip file ...
int inflate_read(char *source,int len,char **dest,int * destsize, int gzip) {
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
	do {
		strm.avail_out = CHUNK;
		strm.next_out = (Bytef*)out;
		ret = inflate(&strm, Z_NO_FLUSH);
		assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
		switch (ret) {
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
	} while (strm.avail_out == 0);

	/* clean up and return */
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int inflate_compare_header(char *source,int len,char *header, int header_sz, int gzip) {
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char out[CHUNK];

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

	/* run inflate() only for one chunk */
	strm.avail_out = CHUNK;
	strm.next_out = (Bytef*)out;
	ret = inflate(&strm, Z_NO_FLUSH);
	assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
	switch (ret) {
	case Z_NEED_DICT:
		ret = Z_DATA_ERROR;     /* and fall through */
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		inflateEnd(&strm);
		return ret;
	}
	have = CHUNK - strm.avail_out;

        ret = memcmp(out,header,header_sz);
                
	/* clean up and return */
	(void)inflateEnd(&strm);
        return ret;
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

void launch_file(void * addr, unsigned len){
        int gzipped_initrd = 0;
	//check if addr point to a gzip file
	unsigned char * gzip_file = (unsigned char *)addr;
	if((gzip_file[0]==0x1F)&&(gzip_file[1]==0x8B)){
		//found a gzip file
		printf(" * Found a gzip file...\n");
		if(inflate_compare_header((char*)addr, len, cpiohdr, 4, 1) == 0){
			gzipped_initrd = 1;
			goto check_magic;
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
        
check_magic:
	//Check for updxell
	if (!memcmp(addr + XELL_FOOTER_OFFSET, XELL_FOOTER, XELL_FOOTER_LENGTH) && len == XELL_SIZE)
	{
		printf(" * Found UpdXeLL binary...\n");
		updateXeLL(addr,len);
	}
	//Check elf header
	else if (!memcmp(addr, elfhdr, 4))
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

int try_load_file(char *filename)
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

	launch_file(buf,r);

	free(buf);
	return 0;
}

void fileloop() {
        char filepath[255];

        int i,j=0;
        for (i = 3; i < 16; i++) {
                if (devoptab_list[i]->structSize) {
                        do{
                           sprintf(filepath, "%s:/%s%c", devoptab_list[i]->name,filenames[j],'\0');
						   try_load_file(filepath);
                           j++;
                        } while(filenames[j] != NULL);
                        j = 0;
                }
        }
}