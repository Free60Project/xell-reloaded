/*
used for zlib support ...
*/

#include <assert.h>
#include <string.h>

#include "zlib.h"

#define CHUNK 16384


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
		//*dest = (char*)realloc(*dest,totalsize);
		memcpy(*dest + totalsize - have,out,have);
		*destsize = totalsize;
	} while (strm.avail_out == 0);

	/* clean up and return */
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}