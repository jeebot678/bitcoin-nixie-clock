#include <cassert>
#include <iostream>
#include <vector>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "../../src/CertificateBundle.cpp"

using Bytes=std::vector<uint8_t>;
Bytes name(X509_NAME* value) {Bytes out(i2d_X509_NAME(value,nullptr));auto p=out.data();i2d_X509_NAME(value,&p);return out;}
Bytes key(EVP_PKEY* value) {Bytes out(i2d_PUBKEY(value,nullptr));auto p=out.data();i2d_PUBKEY(value,&p);return out;}
EVP_PKEY* makeKey() {return EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime256v1");}
mbedtls_x509_buf buf(Bytes& bytes) {mbedtls_x509_buf b;b.p=bytes.data();b.len=bytes.size();return b;}
int main() {
  EVP_PKEY* rootKey=makeKey();EVP_PKEY* otherKey=makeKey();assert(rootKey&&otherKey);
  X509_NAME* rootName=X509_NAME_new();assert(X509_NAME_add_entry_by_txt(rootName,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("Test trusted root"),-1,-1,0));
  Bytes subject=name(rootName),spki=key(rootKey),wrongKey=key(otherKey);
  Bytes bundle{0,1,uint8_t(subject.size()>>8),uint8_t(subject.size()),uint8_t(spki.size()>>8),uint8_t(spki.size())};
  bundle.insert(bundle.end(),subject.begin(),subject.end());bundle.insert(bundle.end(),spki.begin(),spki.end());
  mbedtls_ssl_config conf;assert(arduino_esp_crt_bundle_attach(&conf)==ESP_ERR_INVALID_STATE);
  arduino_esp_crt_bundle_set(bundle.data());assert(arduino_esp_crt_bundle_attach(&conf)==ESP_OK&&conf.verify&&conf.ca);
  mbedtls_x509_crt crossSigned;crossSigned.subject_raw=buf(subject);crossSigned.pk_raw=buf(spki);crossSigned.ca_istrue=1;
  Bytes unknown{1,2,3};crossSigned.issuer_raw=buf(unknown);
  uint32_t flags=MBEDTLS_X509_BADCERT_NOT_TRUSTED;assert(conf.verify(nullptr,&crossSigned,2,&flags)==0&&!flags);
  crossSigned.pk_raw=buf(wrongKey);flags=8;assert(conf.verify(nullptr,&crossSigned,2,&flags)==MBEDTLS_ERR_X509_FATAL_ERROR&&flags==8);
  crossSigned.pk_raw=buf(spki);crossSigned.ca_istrue=0;flags=8;assert(conf.verify(nullptr,&crossSigned,2,&flags)!=0&&flags==8);
  crossSigned.ca_istrue=1;flags=8;assert(conf.verify(nullptr,&crossSigned,0,&flags)!=0&&flags==8);
  for(uint32_t failure:{1U,2U,4U,16U,128U,32768U}) {flags=8|failure;assert(conf.verify(nullptr,&crossSigned,2,&flags)==0&&flags==(8|failure));}
  flags=8|MBEDTLS_X509_BADCERT_BAD_MD;assert(conf.verify(nullptr,&crossSigned,2,&flags)==0&&!flags);
  // Real ECDSA signature verification through the unchanged Mbed TLS path.
  Bytes body{'s','i','g','n','e','d'};EVP_MD_CTX* sign=EVP_MD_CTX_new();assert(EVP_DigestSignInit(sign,nullptr,EVP_sha256(),nullptr,rootKey)==1);
  size_t size=0;assert(EVP_DigestSign(sign,nullptr,&size,body.data(),body.size())==1);Bytes signature(size);
  assert(EVP_DigestSign(sign,signature.data(),&size,body.data(),body.size())==1);signature.resize(size);EVP_MD_CTX_free(sign);
  mbedtls_x509_crt child;child.issuer_raw=buf(subject);child.tbs=buf(body);child.sig=buf(signature);
  flags=8;assert(conf.verify(nullptr,&child,1,&flags)==0&&!flags);
  body[0]^=1;flags=8;assert(conf.verify(nullptr,&child,1,&flags)!=0&&flags==8);body[0]^=1;
  signature[signature.size()-1]^=1;flags=8;assert(conf.verify(nullptr,&child,1,&flags)!=0&&flags==8);
  // Prefixes and arbitrary short DER inputs never read past their bounds.
  for(size_t n=0;n<subject.size();++n) {Bytes prefix(subject.begin(),subject.begin()+n);assert(!tls_bundle::find(s_crt_bundle.crts,1,prefix.data(),n));}
  arduino_esp_crt_bundle_detach(&conf);assert(!conf.verify);
  EVP_PKEY_free(rootKey);EVP_PKEY_free(otherKey);X509_NAME_free(rootName);
  std::cout<<"PASS: actual TLS bundle callback; exact cross-signed root key, wrong keys, CA/depth checks, preserved failures, real ECDSA signatures and bounded lookup\n";
}
