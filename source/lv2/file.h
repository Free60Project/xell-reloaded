/*
used for zlib support ...
*/

enum {
    TYPE_ELF,
    TYPE_INITRD,
    TYPE_KBOOT,
    TYPE_NANDIMAGE,
    TYPE_UPDXELL,
    TYPE_READTXT
};


struct filenames {
    char *filename;
    int filetype;
};

int inflate_read(char *source,int len,char **dest,int * destsize, int gzip);
void wait_and_cleanup_line();
void launch_elf(void * addr, unsigned len, int filetype);
int try_load_file(char *filename, int filetype);
void fileloop();

