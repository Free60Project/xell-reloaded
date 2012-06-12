#ifndef CONFIG_H
#define	CONFIG_H

#define ELF_MAXSIZE (32*1024*1024)

/**
*	Configuration
**/
#define DEFAULT_THEME
//#define NO_PRINT_CONFIG         //commented to display config

/* Filesystem drivers */

#define FS_ISO9660
#define FS_FAT
#define FS_EXT2FS
#define FS_XTAF
//#define FS_NTFS

void mount_all_devices();

void findDevices();

#endif	/* CONFIG_H */

