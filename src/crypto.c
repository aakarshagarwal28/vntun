#include<stdio.h>
#include<string.h>
#include "config.h"

#define KEY MY_KEY

void encrypt(const unsigned char *data, size_t len, unsigned char *en){ 
    int klen = strlen(KEY); 
    for(size_t i=0; i<len; i++){
        en[i] = data[i] ^ KEY[i % klen]; 
    }
}
