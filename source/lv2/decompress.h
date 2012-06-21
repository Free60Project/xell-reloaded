/*
used for zlib support ...
*/
int inflate_read(char *source,int len,char **dest,int * destsize, int gzip);
int inflate_compare_header(char *source,int len,char *header, int header_sz, int gzip);