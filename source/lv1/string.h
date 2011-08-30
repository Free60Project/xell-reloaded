#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <types.h>	/* for size_t */
#include <stddef.h>	/* for NULL */

#ifdef __cplusplus
extern "C" {
#endif

extern char * ___strtok;
extern char * strpbrk(const char *,const char *);
extern char * strtok(char *,const char *);
extern char * strsep(char **,const char *);
extern __kernel_size_t strspn(const char *,const char *);


/*
 * Include machine specific inline routines
 */

#ifndef __HAVE_ARCH_STRCPY
extern char * strcpy(char *,const char *);
size_t strlcpy(char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRNCPY
extern char * strncpy(char *,const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRCAT
extern char * strcat(char *, const char *);
size_t strlcat(char *, const char *, size_t );
#endif
#ifndef __HAVE_ARCH_STRNCAT
extern char * strncat(char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRCMP
extern int strcmp(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRNCMP
extern int strncmp(const char *,const char *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRNICMP
extern int strnicmp(const char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRCHR
extern char * strchr(const char *,int);
#endif
#ifndef __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int);
#endif
#ifndef __HAVE_ARCH_STRSTR
extern char * strstr(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *);
#endif
#ifndef __HAVE_ARCH_STRNLEN
extern __kernel_size_t strnlen(const char *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRDUP
extern char * strdup(const char *);
#endif

#ifndef __HAVE_ARCH_MEMSET
extern void * memset(void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCPY
extern void * memcpy(void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMMOVE
extern void * memmove(void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMSCAN
extern void * memscan(void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCHR
extern void * memchr(const void *,int,__kernel_size_t);
#endif

#if ARCH==7020
#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRCAT
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRLEN
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_BCOPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_BACKWARDS_MEMCPY
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_MEMCHR
#endif

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_STRING_H_ */
