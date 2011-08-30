#include <string.h>
#include "vfs.h"

#include "fsdata.h"

struct vfs_entry_s *search_file(const char *filename)
{
	struct vfs_entry_s *vfs = vfs_entries;
	while (vfs->filename)
		if (!strcmp(filename, vfs->filename))
			return vfs;
		else
			++vfs;
	return 0;
}
