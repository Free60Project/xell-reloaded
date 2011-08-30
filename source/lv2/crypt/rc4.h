#ifndef _RC4_H_
#define _RC4_H_

void  rc4_init(unsigned char *state, unsigned char *key, int len);
void  rc4_crypt(unsigned char *state, unsigned char *data, int len);

#endif	// _RC4_H_

