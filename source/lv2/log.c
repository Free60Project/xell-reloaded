#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE * logfile = NULL;
char * vfs_console_buff = NULL;
size_t vfs_console_len = 0;
extern void (*stdlog_hook)(const char *src, int len);
static int DoLog = 0;

static void log_hook(const char *src, size_t len) {
	if (DoLog) {
		memcpy(&vfs_console_buff[vfs_console_len], src, len);
		vfs_console_len += len;
		if (logfile != NULL && fwrite(src, len, 1, logfile) <= 0)
		{
			printf(" ! logwrite error!\n");
			DoLog = 0;
		}
	}
}

void LogInit() {
	vfs_console_buff = (char*)malloc(LOG_SIZE);
	if (!vfs_console_buff)
	{
		printf("No log available... :( \n");
		return;
	}
	memset(vfs_console_buff, '\0', LOG_SIZE);	
	vfs_console_len = 0;
	stdlog_hook = log_hook;
	DoLog = 1;
	printf("Logging enabled...\n");
}

void LogDeInit() {
	DoLog = 0;
}

int LogWriteFile(const char* logname) {
	if (logfile != NULL)
		fclose(logfile);
	printf("Attempting to open %s to save the log...\n", logname);
	logfile = fopen(logname, "wb");
	if (logfile != NULL && vfs_console_len > 0)
	{
		if (fwrite(vfs_console_buff, sizeof(char), vfs_console_len, logfile) <= 0) {
			printf(" ! logwrite error!\n");
			return -2;
		} else
			printf("Logfile written successfully!\n");
			
	fclose(logfile);
	} else if (logfile == NULL){
		printf("Device Read-Only?\n");
		return -1;
	}	
	return 0;
}
