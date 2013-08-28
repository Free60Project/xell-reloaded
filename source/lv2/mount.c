/**
 * ExtFs and ntfs are buggy need to look at more
 * ATA Dma error ? alignement ?
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <sys/iosupport.h>
#include <diskio/disc_io.h>
#include <byteswap.h>

#include "config.h"

#ifdef FS_FAT 
#include <libfat/fat.h>
#endif
#ifdef FS_EXT2FS
#include <libext2/ext2.h>
#endif
#ifdef FS_NTFS
#include <libntfs/ntfs.h>
#endif
#ifdef FS_XTAF
#include <libxtaf/xtaf.h>
#endif
#ifdef FS_ISO9660
#include <iso9660/iso9660.h>
#endif

extern DISC_INTERFACE xenon_atapi_ops;
extern DISC_INTERFACE xenon_ata_ops;
extern DISC_INTERFACE usb2mass_ops_0;
extern DISC_INTERFACE usb2mass_ops_1;
extern DISC_INTERFACE usb2mass_ops_2;

#define le32_to_cpu(x) bswap_32(x)

#define BYTES_PER_SECTOR 4096
#define NTFS_OEM_ID                         (0x4e54465320202020ULL)

#define PARTITION_TYPE_EMPTY                0x00 /* Empty */
#define PARTITION_TYPE_DOS33_EXTENDED       0x05 /* DOS 3.3+ extended partition */
#define PARTITION_TYPE_NTFS                 0x07 /* Windows NT NTFS */
#define PARTITION_TYPE_WIN95_EXTENDED       0x0F /* Windows 95 extended partition */
#define PARTITION_TYPE_LINUX                0x83 /* EXT2/3/4 */

#define PARTITION_STATUS_NONBOOTABLE        0x00 /* Non-bootable */
#define PARTITION_STATUS_BOOTABLE           0x80 /* Bootable (active) */

#define MBR_SIGNATURE                       (0x55AA)
#define EBR_SIGNATURE                       (0x55AA)

#define BPB_FAT16_fileSysType  0x36
#define BPB_FAT32_fileSysType  0x52

#define T_FAT           1
#define T_NTFS          2
#define T_EXT2          3
#define T_ISO9660       4

static const char FAT_SIG[3] = {'F', 'A', 'T'};

/**
 * PRIMARY_PARTITION - Block device partition record
 */
typedef struct _PARTITION_RECORD {
	u8 status; /* Partition status; see above */
	u8 chs_start[3]; /* Cylinder-head-sector address to first block of partition */
	u8 type; /* Partition type; see above */
	u8 chs_end[3]; /* Cylinder-head-sector address to last block of partition */
	u32 lba_start; /* Local block address to first sector of partition */
	u32 block_count; /* Number of blocks in partition */
} __attribute__((__packed__)) PARTITION_RECORD;

/**
 * MASTER_BOOT_RECORD - Block device master boot record
 */
typedef struct _MASTER_BOOT_RECORD {
	u8 code_area[446]; /* Code area; normally empty */
	PARTITION_RECORD partitions[4]; /* 4 primary partitions */
	u16 signature; /* MBR signature; 0xAA55 */
} __attribute__((__packed__)) MASTER_BOOT_RECORD;

/**
 * struct BIOS_PARAMETER_BLOCK - BIOS parameter block (bpb) structure.
 */
typedef struct {
	u16 bytes_per_sector; /* Size of a sector in bytes. */
	u8 sectors_per_cluster; /* Size of a cluster in sectors. */
	u16 reserved_sectors; /* zero */
	u8 fats; /* zero */
	u16 root_entries; /* zero */
	u16 sectors; /* zero */
	u8 media_type; /* 0xf8 = hard disk */
	u16 sectors_per_fat; /* zero */
	u16 sectors_per_track; /* Required to boot Windows. */
	u16 heads; /* Required to boot Windows. */
	u32 hidden_sectors; /* Offset to the start of the partition */
	u32 large_sectors; /* zero */
} __attribute__((__packed__)) BIOS_PARAMETER_BLOCK;

/**
 * struct NTFS_BOOT_SECTOR - NTFS boot sector structure.
 */
typedef struct {
	u8 jump[3]; /* Irrelevant (jump to boot up code).*/
	u64 oem_id; /* Magic "NTFS    ". */
	BIOS_PARAMETER_BLOCK bpb; /* See BIOS_PARAMETER_BLOCK. */
	u8 physical_drive; /* 0x00 floppy, 0x80 hard disk */
	u8 current_head; /* zero */
	u8 extended_boot_signature; /* 0x80 */
	u8 reserved2; /* zero */
	s64 number_of_sectors; /* Number of sectors in volume. */
	s64 mft_lcn; /* Cluster location of mft data. */
	s64 mftmirr_lcn; /* Cluster location of copy of mft. */
	s8 clusters_per_mft_record; /* Mft record size in clusters. */
	u8 reserved0[3]; /* zero */
	s8 clusters_per_index_record; /* Index block size in clusters. */
	u8 reserved1[3]; /* zero */
	u64 volume_serial_number; /* Irrelevant (serial number). */
	u32 checksum; /* Boot sector checksum. */
	u8 bootstrap[426]; /* Irrelevant (boot up code). */
	u16 end_of_sector_marker; /* End of bootsector magic. */
} __attribute__((__packed__)) NTFS_BOOT_SECTOR;

/**
 * EXTENDED_PARTITION - Block device extended boot record
 */
typedef struct _EXTENDED_BOOT_RECORD {
	u8 code_area[446]; /* Code area; normally empty */
	PARTITION_RECORD partition; /* Primary partition */
	PARTITION_RECORD next_ebr; /* Next extended boot record in the chain */
	u8 reserved[32]; /* Normally empty */
	u16 signature; /* EBR signature; 0xAA55 */
} __attribute__((__packed__)) EXTENDED_BOOT_RECORD;

#define MAX_DEVICES 10

typedef struct {
	char name[50];
	char mount[10];
	int type;
	DISC_INTERFACE* interface;
	sec_t sector;
} DEVICE_STRUCT;

//#define DEBUG_MOUNTALL

#ifdef DEBUG_MOUNTALL
#define debug_printf(fmt, args...) \
        fprintf(stderr, "%s:%d:" fmt, __FUNCTION__, __LINE__, ##args)
#else
#define debug_printf(fmt, args...)
#endif

enum {
	DEVICE_USB_0, // usb
	DEVICE_USB_1, // usb
	DEVICE_USB_2, // usb
	DEVICE_ATA, // hdd
	DEVICE_ATAPI, // cdrom
};

static char *prefix[] = {"uda", "udb", "udc", "sda", "dvd"};

DEVICE_STRUCT part[2][MAX_DEVICES];

static void AddPartition(sec_t sector, int device, int type, int *devnum) {
	int i;

	if (*devnum >= MAX_DEVICES)
		return;

	for (i = 0; i < *devnum; i++)
		if (part[device][i].sector == sector) return; // to avoid mount same partition again

	DISC_INTERFACE *disc = NULL;

	switch(device)
	{
		case DEVICE_USB_0:
		disc = (DISC_INTERFACE *) & usb2mass_ops_0;
		break;		
		case DEVICE_USB_1:
		disc = (DISC_INTERFACE *) & usb2mass_ops_1;
		break;		
		case DEVICE_USB_2:
		disc = (DISC_INTERFACE *) & usb2mass_ops_2;
		break;		
		case DEVICE_ATA:
		disc = (DISC_INTERFACE *) & xenon_ata_ops;
		break;		
		case DEVICE_ATAPI:
		disc = (DISC_INTERFACE *) & xenon_atapi_ops;
		break;
		default:
		return;	
	}

	char mount[10];
	sprintf(mount, "%s%i", prefix[device], *devnum);
	char *name;

	switch (type) {

#ifdef FS_FAT
		case T_FAT:
			if (!fatMount(mount, disc, sector, 2, 64))
				return;
			fatGetVolumeLabel(mount, part[device][*devnum].name);
			break;
#endif
#ifdef FS_NTFS
		case T_NTFS:
			if (!ntfsMount(mount, disc, sector, 2, 64, NTFS_DEFAULT | NTFS_RECOVER))
				return;

			name = (char *) ntfsGetVolumeName(mount);

			if (name && name[0])
				strcpy(part[device][*devnum].name, name);
			else
				part[device][*devnum].name[0] = 0;
			break;
#endif
#ifdef FS_EXT2FS
		case T_EXT2:
			if (!ext2Mount(mount, disc, sector, 2, 128, EXT2_FLAG_DEFAULT))
				return;

			name = (char *) ext2GetVolumeName(mount);

			if (name && name[0])
				strcpy(part[device][*devnum].name, name);
			else
				part[device][*devnum].name[0] = 0;
			break;
#endif
#ifdef FS_ISO9660
		case T_ISO9660:
			if (!ISO9660_Mount(mount, disc))
				return;

			name = (char *) ISO9660_GetVolumeLabel(mount);

			if (name && name[0])
				strcpy(part[device][*devnum].name, name);
			else
				strcpy(part[device][*devnum].name, "DVD");
			break;
#endif
	}

	int c = strlen(part[device][*devnum].name) - 1;

	while (c >= 0 && part[device][*devnum].name[c] == ' ')
		part[device][*devnum].name[c--] = 0;

	strcpy(part[device][*devnum].mount, mount);
	part[device][*devnum].interface = disc;
	part[device][*devnum].sector = sector;
	part[device][*devnum].type = type;
	++*devnum;
}

static int FindPartitions(int device) {
	int i;
	int devnum = 0;

	// clear list
	for (i = 0; i < MAX_DEVICES; i++) {
		part[device][i].name[0] = 0;
		part[device][i].mount[0] = 0;
		part[device][i].sector = 0;
		part[device][i].interface = NULL;
		part[device][i].type = 0;
	}

	DISC_INTERFACE *interface;

	switch(device){
		case DEVICE_ATAPI:
			interface = (DISC_INTERFACE *) & xenon_atapi_ops;
		break;		
		case DEVICE_ATA:
		interface = (DISC_INTERFACE *) & xenon_ata_ops;
		break;		
		case DEVICE_USB_0:
		interface = (DISC_INTERFACE *) & usb2mass_ops_0;
		break;		
		case DEVICE_USB_1:
		interface = (DISC_INTERFACE *) & usb2mass_ops_1;
		break;		
		case DEVICE_USB_2:
		interface = (DISC_INTERFACE *) & usb2mass_ops_2;
		break;
		break;
		default:
			return -1;
	}
		 

	MASTER_BOOT_RECORD mbr;
	PARTITION_RECORD *partition = NULL;
	devnum = 0;
	sec_t part_lba = 0;

	union {
		u8 buffer[BYTES_PER_SECTOR];
		MASTER_BOOT_RECORD mbr;
		EXTENDED_BOOT_RECORD ebr;
		NTFS_BOOT_SECTOR boot;
	} sector;

	if(device == DEVICE_ATAPI){
		AddPartition(0, device, T_ISO9660, &devnum);
		return devnum;
	}

	// Read the first sector on the device
	if (!interface->readSectors(0, 1, &sector.buffer)) {
		//errno = EIO;
		return -1;
	}

	// If this is the devices master boot record
	debug_printf("0x%x\n", sector.mbr.signature);
	if (sector.mbr.signature == MBR_SIGNATURE) {
		memcpy(&mbr, &sector, sizeof (MASTER_BOOT_RECORD));
		debug_printf("Valid Master Boot Record found\n");

		// Search the partition table for all partitions (max. 4 primary partitions)
		for (i = 0; i < 4; i++) {
			partition = &mbr.partitions[i];
			part_lba = le32_to_cpu(mbr.partitions[i].lba_start);
			if (part_lba > interface->sectors())
				continue;

			debug_printf(
					"Partition %i: %s, sector %u, type 0x%x\n",
					i + 1,
					partition->status == PARTITION_STATUS_BOOTABLE ? "bootable (active)"
					: "non-bootable", part_lba, partition->type);

			// Figure out what type of partition this is
			switch (partition->type) {
					// NTFS partition
				case PARTITION_TYPE_NTFS:
				{
					debug_printf("Partition %i: Claims to be NTFS\n", i + 1);

					// Read and validate the NTFS partition
					if (interface->readSectors(part_lba, 1, &sector)) {
						debug_printf("sector.boot.oem_id: 0x%x\n", sector.boot.oem_id);
						debug_printf("NTFS_OEM_ID: 0x%x\n", NTFS_OEM_ID);
						if (sector.boot.oem_id == NTFS_OEM_ID) {
							debug_printf("Partition %i: Valid NTFS boot sector found\n", i + 1);
							AddPartition(part_lba, device, T_NTFS, &devnum);
						} else {
							debug_printf("Partition %i: Invalid NTFS boot sector, not actually NTFS\n", i + 1);
						}
					}
					break;
				}
					// DOS 3.3+ or Windows 95 extended partition
				case PARTITION_TYPE_DOS33_EXTENDED:
				case PARTITION_TYPE_WIN95_EXTENDED:
				{
					debug_printf("Partition %i: Claims to be Extended\n", i + 1);

					// Walk the extended partition chain, finding all NTFS partitions within it
					sec_t ebr_lba = part_lba;
					sec_t next_erb_lba = 0;
					do {
						// Read and validate the extended boot record
						if (interface->readSectors(ebr_lba + next_erb_lba, 1, &sector)) {
							if (sector.ebr.signature == EBR_SIGNATURE) {
								debug_printf(
										"Logical Partition @ %d: %s type 0x%x\n",
										ebr_lba + next_erb_lba,
										sector.ebr.partition.status
										== PARTITION_STATUS_BOOTABLE ? "bootable (active)"
										: "non-bootable",
										sector.ebr.partition.type);

								// Get the start sector of the current partition
								// and the next extended boot record in the chain
								part_lba = ebr_lba + next_erb_lba
										+ le32_to_cpu(
										sector.ebr.partition.lba_start);
								next_erb_lba = le32_to_cpu(
										sector.ebr.next_ebr.lba_start);

								if (sector.ebr.partition.type == PARTITION_TYPE_LINUX) {
									debug_printf("Partition : type ext2/3/4 found\n");
									AddPartition(part_lba, device, T_EXT2, &devnum);
								}// Check if this partition has a valid NTFS boot record
								else if (interface->readSectors(part_lba, 1, &sector)) {
									if (sector.boot.oem_id == NTFS_OEM_ID) {
										debug_printf(
												"Logical Partition @ %d: Valid NTFS boot sector found\n",
												part_lba);
										if (sector.ebr.partition.type
												!= PARTITION_TYPE_NTFS) {
											debug_printf(
													"Logical Partition @ %d: Is NTFS but type is 0x%x; 0x%x was expected\n",
													part_lba,
													sector.ebr.partition.type,
													PARTITION_TYPE_NTFS);
										}
										AddPartition(part_lba, device, T_NTFS, &devnum);
									} else if (!memcmp(sector.buffer
											+ BPB_FAT16_fileSysType, FAT_SIG,
											sizeof (FAT_SIG)) || !memcmp(
											sector.buffer
											+ BPB_FAT32_fileSysType,
											FAT_SIG, sizeof (FAT_SIG))) {
										debug_printf("Partition : Valid FAT boot sector found\n");
										AddPartition(part_lba, device, T_FAT, &devnum);
									}
								}
							} else {
								next_erb_lba = 0;
							}
						}
					} while (next_erb_lba);
					break;
				}
				case PARTITION_TYPE_LINUX:
				{
					debug_printf("Partition %i: Claims to be LINUX\n", i + 1);

					// Read and validate the EXT2 partition
					AddPartition(part_lba, device, T_EXT2, &devnum);
					break;
				}
					// Ignore empty partitions
				case PARTITION_TYPE_EMPTY:
					debug_printf("Partition %i: Claims to be empty\n", i + 1);
					// Unknown or unsupported partition type
				default:
				{
					// Check if this partition has a valid NTFS boot record anyway,
					// it might be misrepresented due to a lazy partition editor
					if (interface->readSectors(part_lba, 1, &sector)) {
						if (sector.boot.oem_id == NTFS_OEM_ID) {
							debug_printf("Partition %i: Valid NTFS boot sector found\n", i + 1);
							if (partition->type != PARTITION_TYPE_NTFS) {
								debug_printf(
										"Partition %i: Is NTFS but type is 0x%x; 0x%x was expected\n",
										i + 1, partition->type,
										PARTITION_TYPE_NTFS);
							}
							AddPartition(part_lba, device, T_NTFS, &devnum);
						} else if (!memcmp(sector.buffer + BPB_FAT16_fileSysType,
								FAT_SIG, sizeof (FAT_SIG)) || !memcmp(
								sector.buffer + BPB_FAT32_fileSysType, FAT_SIG,
								sizeof (FAT_SIG))) {
							debug_printf("Partition : Valid FAT boot sector found\n");
							AddPartition(part_lba, device, T_FAT, &devnum);
						} else {
							debug_printf("Trying : ext partition\n");
							AddPartition(part_lba, device, T_EXT2, &devnum);
						}
					}
					break;
				
				}
			}
		}
	}
	if (devnum == 0) // it is assumed this device has no master boot record or no partitions found
	{
		debug_printf("No Master Boot Record was found or no partitions found!\n");

		// As a last-ditched effort, search the first 64 sectors of the device for stray NTFS/FAT partitions
		for (i = 0; i < 64; i++) {
			if (interface->readSectors(i, 1, &sector)) {
				if (sector.boot.oem_id == NTFS_OEM_ID) {
					debug_printf("Valid NTFS boot sector found at sector %d!\n", i);
					AddPartition(i, device, T_NTFS, &devnum);
					break;
				} else if (!memcmp(sector.buffer + BPB_FAT16_fileSysType, FAT_SIG,
						sizeof (FAT_SIG)) || !memcmp(sector.buffer
						+ BPB_FAT32_fileSysType, FAT_SIG, sizeof (FAT_SIG))) {
					debug_printf("Partition : Valid FAT boot sector found\n");
					AddPartition(i, device, T_FAT, &devnum);
					break;
				} else {
					debug_printf("Trying : ext partition\n");
					AddPartition(part_lba, device, T_EXT2, &devnum);
				}
			}
		}
	}
	return devnum;
}

/*
static void UnmountPartitions(int device) {
	char mount[11];
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		switch (part[device][i].type) {
#ifdef FS_FAT
			case T_FAT:
				part[device][i].type = 0;
				sprintf(mount, "%s:", part[device][i].mount);
				fatUnmount(mount);
				break;
#endif
#ifdef FS_NTFS
			case T_NTFS:
				part[device][i].type = 0;
				ntfsUnmount(part[device][i].mount, false);
				break;
#endif
#ifdef FS_EXT2FS
			case T_EXT2:
				part[device][i].type = 0;
				ext2Unmount(part[device][i].mount);
				break;
#endif
#ifdef FS_ISO9660
			case T_ISO9660:
				part[device][i].type = 0;
				sprintf(mount, "%s:", part[device][i].mount);
				ISO9660_Unmount(mount);
				break;
#endif
		}
		part[device][i].name[0] = 0;
		part[device][i].mount[0] = 0;
		part[device][i].sector = 0;
		part[device][i].interface = NULL;
	}
}
*/

/**
 * Parse mbr for filesystem
 */

int hdd_dvd_mounted = 0; //Prevent mounting the DVD and HDD again...
extern void (*mount_usb_device)(int device);

void mount_usb(int device)
{
	switch (device)
	{
		case 0:
			FindPartitions(DEVICE_USB_0);
			break;
		case 1:
			FindPartitions(DEVICE_USB_1);
			break;
		case 2:
			FindPartitions(DEVICE_USB_2);
			break;
		default:
			printf(" ! Mount USB: Unkown USB device... %d\n", device);
			break;
	}
}

void mount_all_devices() {
	FindPartitions(DEVICE_USB_0);
	FindPartitions(DEVICE_USB_1);
	FindPartitions(DEVICE_USB_2);
	mount_usb_device = mount_usb;
	
	if (hdd_dvd_mounted == 0) //Prevent mounting the DVD and HDD again...
	{
		if (xenon_atapi_ops.isInserted()) {
			FindPartitions(DEVICE_ATAPI);
		}

		if (xenon_ata_ops.isInserted()) {
#ifdef FS_XTAF
			if (XTAFMount() == 0)
#endif
				FindPartitions(DEVICE_ATA);
		}
		hdd_dvd_mounted = 1; //Prevent mounting the DVD and HDD again...
	}
}


char * root_dev = NULL;
static int device_list_size = 0;
char device_list[STD_MAX][10];

int findDevices(){
        int i;
	for (i = 3; i < STD_MAX; i++) {
		if (devoptab_list[i]->structSize) {
			//strcpy(device_list[device_list_size],devoptab_list[i]->name);
			sprintf(device_list[device_list_size], "%s:/", devoptab_list[i]->name);
			printf("Found: %s\r\n", device_list[device_list_size]);
			 
			device_list_size++;
		}
	}

	root_dev = device_list[0];
	return device_list_size;
}
