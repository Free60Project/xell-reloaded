#ifndef __LOG_H
#define __LOG_H

#define LOG_SIZE	1024*1024*10

void LogInit();
void LogDeInit();
int LogWriteFile(const char* logname);

#endif
