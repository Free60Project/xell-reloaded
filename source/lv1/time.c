#include "processor.h"

#define PPC_TIMEBASE_FREQ 50000000L

#undef mftb

static inline unsigned long mftb(void)
{
	unsigned int l, u; 
	asm volatile ("mftbl %0" : "=r" (l));
	asm volatile ("mftbu %0" : "=r" (u));
	return (((unsigned long)u) << 32) | l;
}

unsigned long tb_diff_sec(tb_t *end, tb_t *start)
{
	unsigned long upper, lower;
	upper = end->u - start->u;
	if (start->l > end->l)
		upper--;
	lower = end->l - start->l;
	return ((upper*((unsigned long)0x80000000/(TB_CLOCK/2))) + (lower/ TB_CLOCK));
}

unsigned long tb_diff_msec(tb_t *end, tb_t *start)
{
	unsigned long upper, lower;
	upper = end->u - start->u;
	if (start->l > end->l)
		upper--;
	lower = end->l - start->l;
	return ((upper*((unsigned long)0x80000000/(TB_CLOCK/2000))) + (lower/(TB_CLOCK/1000)));
}

unsigned long tb_diff_usec(tb_t *end, tb_t *start)
{
	unsigned long upper, lower;
	upper = end->u - start->u;
	if (start->l > end->l)
		upper--;
	lower = end->l - start->l;
	return ((upper*((unsigned long)0x80000000/(TB_CLOCK/2000000))) + (lower/(TB_CLOCK/1000000)));
}

static void tdelay(unsigned long i)
{
	unsigned long t = mftb();
	t += i;
	while (mftb() < t) asm volatile("or 31,31,31");
	asm volatile("or 2,2,2");
}

void udelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000000);
}

void mdelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000);
}

void delay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u);
}