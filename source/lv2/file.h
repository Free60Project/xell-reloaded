/*
used for zlib support ...
*/

enum {
    TYPE_ELF,
    TYPE_INITRD,
    TYPE_KBOOT,
    TYPE_NANDIMAGE,
    TYPE_UPDXELL
};


struct filenames {
    char *filename;
    int filetype;
};

int inflate_read(char *source,int len,char **dest,int * destsize, int gzip);
void wait_and_cleanup_line();
int launch_file(void * addr, unsigned len, int filetype);
int try_load_file(char *filename, int filetype);
void fileloop();
void tftp_loop();
