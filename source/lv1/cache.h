#ifndef __CACHE_H
#define __CACHE_H

void flush_code(volatile void *, int len);
void dcache_flush(volatile void *, int len);
void dcache_inv(volatile void *, int len);

#endif
