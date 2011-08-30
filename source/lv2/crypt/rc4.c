//#include "stdafx.h"
#include "rc4.h"


void  rc4_init(unsigned char *state, unsigned char *key, int len)
{
	//FYI state = unsigned char rc4[0x100];

	int i, j=0, t; 

	for (i=0; i < 256; ++i)
		state[i] = i; 

	for (i=0; i < 256; ++i) {
		j = (j + state[i] + key[i % len]) % 256; 
		t = state[i]; 
		state[i] = state[j]; 
		state[j] = t; 
	}	
}

void  rc4_crypt(unsigned char *state, unsigned char *data, int len)
{  
	//FYI state = unsigned char rc4[0x100];
	
	int i=0,j=0,x,t; 

	for (x=0; x < len; ++x)  {
		i = (i + 1) % 256;
		j = (j + state[i]) % 256;
		t = state[i];
		state[i] = state[j];
		state[j] = t;
		*data++ ^= state[(state[i] + state[j]) % 256];
	}
}

