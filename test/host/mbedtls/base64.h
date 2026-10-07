#pragma once
#include <openssl/evp.h>
#include <cstring>
inline int mbedtls_base64_decode(unsigned char*out,size_t capacity,size_t*count,const unsigned char*input,size_t length){
  if(length>400)return -1;unsigned char tmp[512];int n=EVP_DecodeBlock(tmp,input,int(length));if(n<0)return -1;
  if(length&&input[length-1]=='=')--n;if(length>1&&input[length-2]=='=')--n;
  if(size_t(n)>capacity)return -1;memcpy(out,tmp,n);*count=size_t(n);return 0;
}
