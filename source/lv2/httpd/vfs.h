#ifndef __vfs_h
#define __vfs_h

struct vfs_entry_s
{
	const char *filename;
	const char *data;
	int len;
	const char *mime_type;
	int flags;
};

struct vfs_entry_s *search_file(const char *filename);

#endif
