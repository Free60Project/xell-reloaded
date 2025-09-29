/*
used for zlib support ...
*/

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <console/console.h>
#include <elf/elf.h>
#include <network/network.h>
#include <ppc/timebase.h>
#include <sys/iosupport.h>
#include <usb/usbmain.h>
#include <xb360/xb360.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xetypes.h>

#include "../lv1/puff/puff.h"
#include "config.h"
#include "file.h"
#include "kboot/kbootconf.h"
#include "tftp/tftp.h"

#define GZIP_HEADER_SIZE 10

extern char dt_blob_start[];
extern char dt_blob_end[];

const unsigned char elfhdr[] = {0x7f, 'E', 'L', 'F'};
const unsigned char cpiohdr[] = {0x30, 0x37, 0x30, 0x37};
const unsigned char kboothdr[] = "#KBOOTCONFIG";

struct filenames filelist[] = {{"kboot.conf", TYPE_KBOOT},
                               {"xenon.elf", TYPE_ELF},
                               {"xenon.z", TYPE_ELF},
                               {"vmlinux", TYPE_ELF},
                               {"updxell.bin",TYPE_UPDXELL},
                               {"updflash.bin",TYPE_NANDIMAGE},
                               {NULL, TYPE_INVALID}};

void wait_and_cleanup_line() {
  uint64_t t = mftb();
  while (tb_diff_msec(mftb(), t) < 200) { // yield to network
    network_poll();
  }
  console_clrline();
}

int launch_file(void *addr, unsigned len, int filetype) {
  int ret = 0;
  unsigned char *gzip_file;
  switch (filetype) {

  case TYPE_ELF:
    // check if addr point to a gzip file
    gzip_file = (unsigned char *)addr;
    if ((gzip_file[0] == 0x1F) && (gzip_file[1] == 0x8B)) {
      // found a gzip file
      printf(" * Found a gzip file, unpacking...\n");
      char *dest = malloc(ELF_MAXSIZE);
      long unsigned int destsize = ELF_MAXSIZE;
      int err = puff((unsigned char *)dest, &destsize, &gzip_file[GZIP_HEADER_SIZE], (long unsigned int *)&len);
      if (err == 0) {
        // relocate elf ...
        memcpy(addr, dest, destsize);
        printf(" * Successfully unpacked %li bytes...\n", destsize);
        free(dest);
        len = destsize;
      } else {
        printf(" * Unpacking failed with error %i...\n", err);
        free(dest);
        return -1;
      }
    }
    if (memcmp(addr, elfhdr, 4))
      return -1;
    printf(" * Launching ELF...\n");
    ret = elf_runWithDeviceTree(addr, len, dt_blob_start,
                                dt_blob_end - dt_blob_start);
    break;
  case TYPE_INITRD:
    printf(" * Loading initrd into memory ...\n");
    ret = kernel_prepare_initrd(addr, len);
    break;
  case TYPE_KBOOT:
    printf(" * Loading kboot.conf ...\n");
    ret = try_kbootconf(addr, len);
    break;
  // This shit is broken!
  //     case TYPE_UPDXELL:
   //if (memcmp(addr + XELL_FOOTER_OFFSET, XELL_FOOTER, XELL_FOOTER_LENGTH) ||
  // len != XELL_SIZE) 	return -1;
  //         printf(" * Loading UpdXeLL binary...\n");
  //         ret = updateXeLL(addr,len);
  //         break;
  default:
    printf("! Unsupported filetype supplied!\n");
  }
  return ret;
}

int try_load_file(char *filename, int filetype) {
  int ret;
  if (filetype == TYPE_NANDIMAGE) {
    try_rawflash(filename);
    return -1;
  }

  if (filetype == TYPE_UPDXELL) {
    updateXeLL(filename);
    return -1;
  }

  wait_and_cleanup_line();
  printf("Trying %s...\r", filename);

  struct stat s;
  memset(&s, 0, sizeof(struct stat));
  stat(filename, &s);

  long size = s.st_size;

  if (size <= 0)
    return -1; // Size is invalid

  FILE *f = fopen(filename, "r");

  if (f == NULL)
    return -1; // File wasn't opened...

  void *buf = malloc(size);

  printf("\n * '%s' found, loading %ld...\n", filename, size);
  int r = fread(buf, 1, size, f);
  if (r != size) {
    fclose(f);
    free(buf);
    return -1;
  }

  if (filetype == TYPE_ELF) {
    char *argv[] = {
        filename,
    };
    int argc = sizeof(argv) / sizeof(char *);

    elf_setArgcArgv(argc, argv);
  }

  ret = launch_file(buf, r, filetype);
  
  fclose(f);
  free(buf);
  return ret;
}

void fileloop() {
  char filepath[255];

  int i, j = 0;
  for (i = 3; i < 16; i++) {
    if (devoptab_list[i]->structSize) {
      do {
        usb_do_poll();
        if (!devoptab_list[i]->structSize)
          break;
        sprintf(filepath, "%s:/%s", devoptab_list[i]->name,
                filelist[j].filename);
        if ((filelist[j].filetype == TYPE_UPDXELL ||
             filelist[j].filetype == TYPE_NANDIMAGE) &&
            (xenon_get_console_type() == REV_CORONA_PHISON)) {
          wait_and_cleanup_line();
          printf("MMC Console Detected! Skipping %s...\r", filepath);
          j++;
        } else {
          try_load_file(filepath, filelist[j].filetype);
          j++;
        }
      } while (filelist[j].filename != NULL);
      j = 0;
    }
  }
}

void tftp_loop(ip_addr_t server) {
  int i = 0;
  do {
    if ((filelist[i].filetype == TYPE_UPDXELL ||
         filelist[i].filetype == TYPE_NANDIMAGE) &&
        (xenon_get_console_type() == REV_CORONA_PHISON)) {
      wait_and_cleanup_line();
      printf("Skipping TFTP %s:%s... MMC Detected!\r", ipaddr_ntoa(&server),
             filelist[i].filename);
      i++;
    } else {
      wait_and_cleanup_line();
      printf("Trying TFTP %s:%s... \r", ipaddr_ntoa(&server),
             filelist[i].filename);
      boot_tftp(server, filelist[i].filename, filelist[i].filetype);
      i++;
    }
    network_poll();
  } while (filelist[i].filename != NULL);

  wait_and_cleanup_line();
  printf("Trying TFTP %s:%s...\r", ipaddr_ntoa(&server), boot_file_name());
  /* Assume that bootfile delivered via DHCP is an ELF */
  boot_tftp(server, boot_file_name(), TYPE_ELF);
}
