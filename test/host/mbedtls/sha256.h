#pragma once
#include <openssl/evp.h>
struct mbedtls_sha256_context{EVP_MD_CTX*ctx=nullptr;};
inline void mbedtls_sha256_init(mbedtls_sha256_context*c){c->ctx=EVP_MD_CTX_new();}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int){return EVP_DigestInit_ex(c->ctx,EVP_sha256(),nullptr)==1?0:-1;}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const unsigned char*data,size_t n){return EVP_DigestUpdate(c->ctx,data,n)==1?0:-1;}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,unsigned char*out){unsigned n;return EVP_DigestFinal_ex(c->ctx,out,&n)==1&&n==32?0:-1;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*c){EVP_MD_CTX_free(c->ctx);}
inline int mbedtls_sha256_ret(const unsigned char*data,size_t n,unsigned char*out,int){unsigned size;return EVP_Digest(data,n,out,&size,EVP_sha256(),nullptr)==1&&size==32?0:-1;}
