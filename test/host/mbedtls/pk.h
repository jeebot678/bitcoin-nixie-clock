#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
constexpr int MBEDTLS_MD_SHA256=1;
struct mbedtls_pk_context{EVP_PKEY*key=nullptr;};
inline void mbedtls_pk_init(mbedtls_pk_context*c){c->key=nullptr;}
inline int mbedtls_pk_parse_public_key(mbedtls_pk_context*c,const unsigned char*data,size_t n){BIO*b=BIO_new_mem_buf(data,int(n));c->key=PEM_read_bio_PUBKEY(b,nullptr,nullptr,nullptr);BIO_free(b);return c->key?0:-1;}
inline size_t mbedtls_pk_get_bitlen(mbedtls_pk_context*c){return EVP_PKEY_get_bits(c->key);}
inline int mbedtls_pk_verify(mbedtls_pk_context*c,int,const unsigned char*hash,size_t n,const unsigned char*signature,size_t size){EVP_PKEY_CTX*ctx=EVP_PKEY_CTX_new(c->key,nullptr);int ok=EVP_PKEY_verify_init(ctx)>0&&EVP_PKEY_CTX_set_rsa_padding(ctx,RSA_PKCS1_PADDING)>0&&EVP_PKEY_CTX_set_signature_md(ctx,EVP_sha256())>0&&EVP_PKEY_verify(ctx,signature,size,hash,n)==1;EVP_PKEY_CTX_free(ctx);return ok?0:-1;}
inline void mbedtls_pk_free(mbedtls_pk_context*c){EVP_PKEY_free(c->key);}
