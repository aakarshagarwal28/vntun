#include<string.h>
#include<stdlib.h>
#include<stdio.h> 
#include "config.h"

ssize_t attach_header(unsigned char *enc, size_t n, unsigned char *payload){
    
    memcpy(payload, MAGIC_HEADER, 8); 
    memcpy(payload + 8, enc, n); 
    
    return 8 + n; 
}