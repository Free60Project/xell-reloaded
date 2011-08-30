
#ifndef __MAIN_H
#define	__MAIN_H

#define LOADER_RAW         0x8000000004000000ULL
#define LOADER_MAXSIZE     0x1000000

#define XELL_OK			0
#define XELL_ERROR		1

int main(void);

void execute_elf_at(void *addr);
int launch_elf(void * addr, unsigned len);//Load gzip xell

/****
* Execption vector (information from libxenon)
***/
#define EX_RESET 					0x00000100
#define EX_MACHINE_CHECK                                0X00000200
#define EX_DSI						0X00000300
#define EX_DATA_SEGMENT                                 0X00000380
#define EX_ISI						0X00000400
#define	EX_INSTRUCTION_SEGMENT                          0X00000480

#define EX_INTERRUPT                                    0X00000500
#define EX_ALIGNMENT                                    0X00000600
#define EX_PROGRAM					0X00000700
#define EX_FLOATING_POINT                               0X00000800

#define EX_DECREMENTER                                  0X00000900
#define EX_SYSTEM_CALL                                  0X00000C00
#define EX_TRACE					0X00000D00
#define EX_PERFORMANCE                                  0X00000F00

#define EX_IABR						0X00001300
#define EX_RESERVED					0X00001400
#define EX_THERMAL					0X00001700

#endif	/* MAIN_H */

