#include <setjmp.h>

#include "main.h"
#include "types.h"
#include "vsprintf.h"
#include "string.h"
#include "elf_abi.h"
#include "cache.h"
#include "time.h"

#include "puff/puff.h"

#define GZIP_HEADER_SIZE 10
#define GZIP_FOOTER_SIZE 8
#define XELL_SIZE (256*1024)
#define XELL_FOOTER_SIZE 16
#define XELL_STAGE1_SIZE (16*1024)
#define XELL_STAGE2_SIZE (XELL_SIZE-XELL_STAGE1_SIZE-XELL_FOOTER_SIZE-GZIP_HEADER_SIZE-GZIP_FOOTER_SIZE)

#define OCR_LAND_OFFSET 0x1000ULL
#define OCR_LAND_ADDR (0x8000020000010000ULL|OCR_LAND_OFFSET)
#define OCR_LAND_MAGIC (((OCR_LAND_OFFSET ^ 0xffff)<<48)  | OCR_LAND_OFFSET<<32 | ((OCR_LAND_OFFSET ^ 0xffff)<<16) | OCR_LAND_OFFSET)

extern char _start[];
extern char bss_start[], bss_end[];
extern char start_from_rom[];
extern char other_threads_startup[], other_threads_startup_end[];
volatile unsigned long secondary_hold_addr = 1;
volatile int processors_online[6] = {1};

#ifdef HACK_JTAG
volatile long wakeup_cpus = 0;
#else
volatile long wakeup_cpus = 1;
#endif

void jump(unsigned long dtc, unsigned long kernel_base, unsigned long null, unsigned long reladdr, unsigned long hrmor);

static inline uint64_t ld(volatile void *addr)
{
	uint64_t l;
	asm volatile ("ld %0, 0(%1)" : "=r" (l) : "b" (addr));
	return l;
}


static inline void  std(volatile void *addr, uint64_t v)
{
	asm volatile ("std %1, 0(%0)" : : "b" (addr), "r" (v));
}

static inline uint32_t bswap_32(uint32_t t)
{
	return ((t & 0xFF) << 24) | ((t & 0xFF00) << 8) | ((t & 0xFF0000) >> 8) | ((t & 0xFF000000) >> 24);
}

static void putch(unsigned char c)
{
	while (!((*(volatile uint32_t*)0x80000200ea001018)&0x02000000));
	*(volatile uint32_t*)0x80000200ea001014 = (c << 24) & 0xFF000000;
}

int kbhit(void)
{
	uint32_t status;
	
	do
		status = *(volatile uint32_t*)0x80000200ea001018;
	while (status & ~0x03000000);
	
	return !!(status & 0x01000000);
}

int getchar(void)
{
	while (!kbhit());
	return (*(volatile uint32_t*)0x80000200ea001010) >> 24;
}

int putchar(int c)
{
#ifndef CYGNOS
	if (c == '\n')
		putch('\r');
#endif
	putch(c);
	return 0;
}

void putstring(const char *c)
{
	while (*c)
		putchar(*c++);
}

int puts(const char *c)
{
	putstring(c);
	putstring("\n");
	return 0;
}

/* e_ident */
#define IS_ELF(ehdr) ((ehdr).e_ident[EI_MAG0] == ELFMAG0 && \
                      (ehdr).e_ident[EI_MAG1] == ELFMAG1 && \
                      (ehdr).e_ident[EI_MAG2] == ELFMAG2 && \
                      (ehdr).e_ident[EI_MAG3] == ELFMAG3)

unsigned long load_elf_image(void *addr)
{
	Elf32_Ehdr *ehdr;
	Elf32_Shdr *shdr;
	unsigned char *strtab = 0;
	int i;

	ehdr = (Elf32_Ehdr *) addr;
	
	shdr = (Elf32_Shdr *) (addr + ehdr->e_shoff + (ehdr->e_shstrndx * sizeof (Elf32_Shdr)));

	if (shdr->sh_type == SHT_STRTAB)
		strtab = (unsigned char *) (addr + shdr->sh_offset);

	for (i = 0; i < ehdr->e_shnum; ++i)
	{
		shdr = (Elf32_Shdr *) (addr + ehdr->e_shoff +
				(i * sizeof (Elf32_Shdr)));

		if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_size == 0)
			continue;

		if (strtab) {
			printf("0x%08x 0x%08x, %sing %s...",
				(int) shdr->sh_addr,
				(int) shdr->sh_size,
				(shdr->sh_type == SHT_NOBITS) ?
					"Clear" : "Load",
				&strtab[shdr->sh_name]);
		}

		void *target = (void*)(((unsigned long)0x8000000000000000UL) | shdr->sh_addr);

		if (shdr->sh_type == SHT_NOBITS) {
			memset (target, 0, shdr->sh_size);
		} else {
			memcpy ((void *) target, 
				(unsigned char *) addr + shdr->sh_offset,
				shdr->sh_size);
		}
		flush_code (target, shdr->sh_size);
		puts("done");
	}
	
	return ehdr->e_entry;
}

void execute_elf_at(void *addr)
{
	printf(" * Loading ELF file...\n");
	void *entry = (void*)load_elf_image(addr);
	
	printf(" * GO (entrypoint: %p)\n", entry);

	secondary_hold_addr = ((long)entry) | 0x8000000000000060UL;
	
	jump(0, (long)entry, 0, (long)entry, 0);
}

int get_online_processors(void)
{
	int i;
	int res = 0;
	for (i=0; i<6; ++i)
		if (processors_online[i])
			res |= 1<<i;
	return res;
}

void place_jump(void *addr, void *_target)
{
	unsigned long target = (unsigned long)_target;
	dcache_flush(addr - 0x80, 0x100);
	*(volatile uint32_t*)(addr - 0x18 + 0) = 0x3c600000 | ((target >> 48) & 0xFFFF);
	*(volatile uint32_t*)(addr - 0x18 + 4) = 0x786307c6;
	*(volatile uint32_t*)(addr - 0x18 + 8) = 0x64630000 | ((target >> 16) & 0xFFFF);
	*(volatile uint32_t*)(addr - 0x18 + 0xc) = 0x60630000 | (target & 0xFFFF);
	*(volatile uint32_t*)(addr - 0x18 + 0x10) = 0x7c6803a6;
	*(volatile uint32_t*)(addr - 0x18 + 0x14) = 0x4e800021;
	flush_code(addr-0x18, 0x18);
	*(volatile uint32_t*)(addr + 0) = 0x4bffffe8;
	flush_code(addr, 0x80);
}

void init_soc(void)
{
	void *soc_29 = (void*)0x8000020000030000;
	void *soc_31 = (void*)0x8000020000040000;
	void *soc_30 = (void*)0x8000020000060000;
	void *soc_03 = (void*)0x8000020000048000;

	u64 v;

	v=ld(soc_31+0x3000);
	v&=(((u64)-0x1a)<<42)|(((u64)-0x1a)>>22);
	v|=(u64)3<<43;
	std(soc_31+0x3000,v);

	std(soc_31+0x3110,(((u64)-2)<<46)|(((u64)-2)>>18));

	std(soc_29+0x3110,(((u64)-2)<<41)|(((u64)-2)>>23));

	v=ld(soc_30+0x700);
	v|=(u64)7<<53;
	std(soc_30+0x700,v);

	v=ld(soc_30+0x840);
	v&=(((u64)-2)<<46)|(((u64)-2)>>18);
	v|=(u64)0xf<<42;
	std(soc_30+0x840,v);

	v=ld(soc_31);
	v&=((u64)0xffffffffFFFC7FC0<<46)|((u64)0xffffffffFFFC7FC0>>18);
	v|=(u64)0x4009<<48;
	std(soc_31,v);

	v=ld(soc_03);
	v&=(((u64)-0x30)<<57)|(((u64)-0x30)>>7);
	v|=(u64)0x401<<46;
	std(soc_03,v);

	v=ld(soc_29);
	v|=(u64)1<<62;
	std(soc_29,v);
}

void fix_hrmor();

int start_from_exploit=0;

int start(int pir, unsigned long hrmor, unsigned long pvr)
{

	secondary_hold_addr = 0;

#ifdef HACK_JTAG
	//execption vector
	int exc[] = {
		EX_RESET, EX_MACHINE_CHECK, EX_DSI, EX_DATA_SEGMENT,
		EX_ISI, EX_INSTRUCTION_SEGMENT, EX_INTERRUPT, EX_ALIGNMENT,
		EX_PROGRAM, EX_FLOATING_POINT, EX_DECREMENTER, 0x980,
		EX_SYSTEM_CALL, EX_TRACE, EX_PERFORMANCE, 0xF20,
		EX_IABR, 0x1600, EX_THERMAL, 0x1800
	};

	int i;
#endif
		
	/* initialize BSS first. DO NOT INSERT CODE BEFORE THIS! */
	unsigned char *p = (unsigned char*)bss_start;
	memset(p, 0, bss_end - bss_start);

#ifdef CYGNOS
	/* set UART to 38400, 8, N, 1 */
	*(volatile uint32_t*)0x80000200ea00101c = 0xae010000;
#else
	/* set UART to 115400, 8, N, 1 */
	*(volatile uint32_t*)0x80000200ea00101c = 0xe6010000;
#endif

	printf("\nXeLL - First stage\n");

	if(!wakeup_cpus)
	{
#ifdef HACK_JTAG
		printf(" * Attempting to catch all CPUs...\n");


		for (i=0; i<sizeof(exc)/sizeof(*exc); ++i)
			place_jump((void*)hrmor + exc[i], start_from_rom);


		printf(" * place_jump ...\n");

		place_jump((void*)0x8000000000000700, start_from_rom);

		printf(" * while ...\n");

		while (get_online_processors() != 0x3f)
		{
			printf("CPUs online: %02x..\n", get_online_processors());
			mdelay(10);

			for (i=1; i<6; ++i)
			{
				*(volatile uint64_t*)(0x8000020000050070ULL + i * 0x1000) = 0x7c;
				*(volatile uint64_t*)(0x8000020000050068ULL + i * 0x1000) = 0;
				(void)*(volatile uint64_t*)(0x8000020000050008ULL + i * 0x1000);
				while (*(volatile uint64_t*)(0x8000020000050050ULL + i * 0x1000) != 0x7C);
			}

			*(uint64_t*)(0x8000020000052010ULL) = 0x3e0078;
		}

		fix_hrmor();

		/* re-reset interrupt controllers. especially, remove their pending IPI IRQs. */
		for (i=1; i<6; ++i)
		{
			*(uint64_t*)(0x8000020000050068ULL + i * 0x1000) = 0x74;
			while (*(volatile uint64_t*)(0x8000020000050050ULL + i * 0x1000) != 0x7C);
		}
#endif
	}
	else
	{
		init_soc();

		printf(" * Attempting to wakeup all CPUs...\n");

		// place startup code

			// reset exception (odd threads startup)
		place_jump((void*)0x8000000000000100, other_threads_startup);

			// copy startup code to on-chip RAM (even threads startup)
		memcpy((void*)OCR_LAND_ADDR, other_threads_startup, other_threads_startup_end - other_threads_startup);
		flush_code ((void*)OCR_LAND_ADDR, other_threads_startup_end - other_threads_startup);

		// setup 1BL secondary hold addresses

		void *sec_hold_addrs = (void*)0x800002000001ff80;

		std(sec_hold_addrs + 0x68, OCR_LAND_MAGIC);
		std(sec_hold_addrs + 0x70, OCR_LAND_MAGIC);
		std(sec_hold_addrs + 0x78, OCR_LAND_MAGIC);

		// startup threads

		void *irq_cntrl = (void*)0x8000020000050000;

		std(irq_cntrl + 0x2070, 0x7c);
		std(irq_cntrl + 0x2008, 0);
		std(irq_cntrl + 0x2000, 4);

		std(irq_cntrl + 0x4070, 0x7c);
		std(irq_cntrl + 0x4008, 0);
		std(irq_cntrl + 0x4000, 0x10);

		std(irq_cntrl + 0x10, 0x140078);

		mtspr(152, 0xc00000);  // CTRL.TE{0,1} = 11

		while (get_online_processors() != 0x3f)
		{
			printf("CPUs online: %02x..\n", get_online_processors());
			mdelay(10);
		}

		// enable IRQ controllers

		int i;
		for(i=0;i<6;i++){
			std(irq_cntrl + i*0x1000 + 0x70, 0x7c);
			std(irq_cntrl + i*0x1000 + 8, 0x7c);
			std(irq_cntrl + i*0x1000, 1<<i); 
		}
 
	}

	printf("CPUs online: %02x..\n", get_online_processors());
	printf(" * success.\n");

	start_from_exploit=1;

	return main();
}

// fake setjmp implementation for puff

void longjmp(jmp_buf jb,int retval){
	printf("[ERROR] longjmp retval=%d\n",retval);
	for(;;);
}

int setjmp(jmp_buf jb){
	return 0;
}

static unsigned char * stage2 = (unsigned char *)_start+XELL_STAGE1_SIZE;
static unsigned char stage2_elf[1024*1024];

int main() {

	if (!start_from_exploit)
		printf("\nXeLL - First stage\n");

	/* remove any characters left from bootup */
	while (kbhit())
		getchar();

	printf(" * Decompressing stage 2...\n");

	unsigned long destsize=sizeof(stage2_elf), srcsize=XELL_STAGE2_SIZE;

	if (stage2[0]!=0x1f || stage2[1]!=0x8b || stage2[2]!=0x08 || stage2[3]!=0x00){
		printf("[ERROR] bad gzip header\n");
		goto end;
	}

	int res=puff(stage2_elf,&destsize,&stage2[GZIP_HEADER_SIZE],&srcsize);

	if (res){
		printf("[ERROR] decompression failed (srcsize=%ld destsize=%ld res=%d)\n",srcsize,destsize,res);
		goto end;
	}

	execute_elf_at(stage2_elf);

end:
	printf(" * looping...\n");
	for(;;);

	return 0;
}
