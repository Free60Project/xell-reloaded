#ifndef __processor_h
#define __processor_h

typedef struct
{
	unsigned long l, u;
} tb_t;

#define __stringify(rn) #rn

#define mfdcr(rn) ({unsigned int rval; \
      asm volatile("mfdcr %0," __stringify(rn) \
             : "=r" (rval)); rval;})
#define mtdcr(rn, v)  asm volatile("mtdcr " __stringify(rn) ",%0" : : "r" (v))

#define mfmsr()   ({unsigned long rval; \
      asm volatile("mfmsr %0" : "=r" (rval)); rval;})
#define mtmsr(v)  asm volatile("mtmsr %0" : : "r" (v))

#define mfdec()   ({unsigned long rval; \
      asm volatile("mfdec %0" : "=r" (rval)); rval;})
#define mtdec(v)  asm volatile("mtdec %0" : : "r" (v))

#define mfspr(rn) ({unsigned long rval; \
      asm volatile("mfspr %0," __stringify(rn) \
             : "=r" (rval)); rval;})
#define mtspr(rn, v)  asm volatile("mtspr " __stringify(rn) ",%0" : : "r" (v))

#define mftb(rval) ({unsigned long u; do { \
	 asm volatile ("mftbu %0" : "=r" (u)); \
	 asm volatile ("mftb %0" : "=r" ((rval)->l)); \
	 asm volatile ("mftbu %0" : "=r" ((rval)->u)); \
	 } while(u != ((rval)->u)); })

	/* checkme */
#define TB_CLOCK  45000000

#endif
