#pragma once
// OpenSSL adapter for exercising the actual embedded bundle callback.
#include <openssl/evp.h>
#include "esp_err.h"
#include <cstdint>
constexpr uint32_t MBEDTLS_X509_BADCERT_NOT_TRUSTED=8, MBEDTLS_X509_BADCERT_BAD_MD=16384;
constexpr int MBEDTLS_ERR_X509_FATAL_ERROR=-12288, MBEDTLS_MD_MAX_SIZE=64;
struct mbedtls_x509_buf { size_t len=0; unsigned char* p=nullptr; };
struct mbedtls_pk_context { EVP_PKEY* key=nullptr; };
struct mbedtls_x509_crt {
  mbedtls_x509_buf subject_raw,issuer_raw,pk_raw,tbs,sig;
  mbedtls_pk_context pk;
  int ca_istrue=0,sig_pk=EVP_PKEY_EC,sig_md=NID_sha256;
  void* sig_opts=nullptr;
};
struct mbedtls_md_info_t { const EVP_MD* digest; };
inline void mbedtls_x509_crt_init(mbedtls_x509_crt* crt) { *crt={}; }
inline void mbedtls_x509_crt_free(mbedtls_x509_crt* crt) { EVP_PKEY_free(crt->pk.key);crt->pk.key=nullptr; }
inline int mbedtls_pk_parse_public_key(mbedtls_pk_context* pk,const unsigned char* p,size_t n) {
  pk->key=d2i_PUBKEY(nullptr,&p,long(n));return pk->key?0:-1;
}
inline bool mbedtls_pk_can_do(mbedtls_pk_context* pk,int type) { return EVP_PKEY_base_id(pk->key)==type; }
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int type) {
  static mbedtls_md_info_t info{EVP_sha256()};return type==NID_sha256?&info:nullptr;
}
inline int mbedtls_md(const mbedtls_md_info_t* md,const unsigned char* p,size_t n,unsigned char* out) {
  unsigned size;return md&&EVP_Digest(p,n,out,&size,md->digest,nullptr)==1?0:-1;
}
inline size_t mbedtls_md_get_size(const mbedtls_md_info_t* md) { return size_t(EVP_MD_get_size(md->digest)); }
inline int mbedtls_pk_verify_ext(int,void*,mbedtls_pk_context* pk,int md,const unsigned char* hash,size_t n,const unsigned char* sig,size_t length) {
  EVP_PKEY_CTX* ctx=EVP_PKEY_CTX_new(pk->key,nullptr);
  int ok=ctx&&EVP_PKEY_verify_init(ctx)>0&&md==NID_sha256&&EVP_PKEY_CTX_set_signature_md(ctx,EVP_sha256())>0&&EVP_PKEY_verify(ctx,sig,length,hash,n)==1;
  EVP_PKEY_CTX_free(ctx);return ok?0:-1;
}
using VerifyCallback=int(*)(void*,mbedtls_x509_crt*,int,uint32_t*);
struct mbedtls_ssl_config { VerifyCallback verify=nullptr;mbedtls_x509_crt* ca=nullptr; };
inline void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config* conf,mbedtls_x509_crt* ca,void*) {conf->ca=ca;}
inline void mbedtls_ssl_conf_verify(mbedtls_ssl_config* conf,VerifyCallback verify,void*) {conf->verify=verify;}
extern "C" {
esp_err_t arduino_esp_crt_bundle_attach(void*);
void arduino_esp_crt_bundle_detach(mbedtls_ssl_config*);
void arduino_esp_crt_bundle_set(const uint8_t*);
}
