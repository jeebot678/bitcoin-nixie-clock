// Copyright 2018-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <string.h>
#include <esp_system.h>
#include <esp32-hal-log.h>
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "TrustedBundle.h"

// Project override of Arduino-ESP32 2.0.17 bundle verification.
// Recognize cross-signed roots by their trusted subject AND public key.
// Mbed TLS still verifies the lower chain, hostname, dates and constraints.

#define BUNDLE_HEADER_OFFSET 2
#define CRT_HEADER_OFFSET 4

/* a dummy certificate so that
 * cacert_ptr passes non-NULL check during handshake */
static mbedtls_x509_crt s_dummy_crt;


typedef struct crt_bundle_t {
    const uint8_t **crts;
    uint16_t num_certs;
    size_t x509_crt_bundle_len;
} crt_bundle_t;

static crt_bundle_t s_crt_bundle;

static int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int data, uint32_t *flags);
static int esp_crt_check_signature(mbedtls_x509_crt *child, const uint8_t *pub_key_buf, size_t pub_key_len);


static int esp_crt_check_signature(mbedtls_x509_crt *child, const uint8_t *pub_key_buf, size_t pub_key_len)
{
    int ret = 0;
    mbedtls_x509_crt parent;
    const mbedtls_md_info_t *md_info;
    unsigned char hash[MBEDTLS_MD_MAX_SIZE];

    mbedtls_x509_crt_init(&parent);

    if ( (ret = mbedtls_pk_parse_public_key(&parent.pk, pub_key_buf, pub_key_len) ) != 0) {
        log_e("PK parse failed with error %X", ret);
        goto cleanup;
    }


    // Fast check to avoid expensive computations when not necessary
    if (!mbedtls_pk_can_do(&parent.pk, child->sig_pk)) {
        log_e("Simple compare failed");
        ret = -1;
        goto cleanup;
    }

    md_info = mbedtls_md_info_from_type(child->sig_md);
    if (!md_info) { ret = -1; goto cleanup; }
    if ( (ret = mbedtls_md( md_info, child->tbs.p, child->tbs.len, hash )) != 0 ) {
        log_e("Internal mbedTLS error %X", ret);
        goto cleanup;
    }

    if ( (ret = mbedtls_pk_verify_ext( child->sig_pk, child->sig_opts, &parent.pk,
                                       child->sig_md, hash, mbedtls_md_get_size( md_info ),
                                       child->sig.p, child->sig.len )) != 0 ) {

        log_e("PK verify failed with error %X", ret);
        goto cleanup;
    }
cleanup:
    mbedtls_x509_crt_free(&parent);

    return ret;
}


/* This callback is called for every certificate in the chain. If the chain
 * is proper each intermediate certificate is validated through its parent
 * in the x509_crt_verify_chain() function. So this callback should
 * only verify the first untrusted link in the chain is signed by the
 * root certificate in the trusted bundle
*/
int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)buf;
    mbedtls_x509_crt *child = crt;

    /* It's OK for a trusted cert to have a weak signature hash alg.
       as we already trust this certificate */
    uint32_t flags_filtered = *flags & ~(MBEDTLS_X509_BADCERT_BAD_MD);

    if (flags_filtered != MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
        return 0;
    }


    if (s_crt_bundle.crts == NULL) {
        log_e("No certificates in bundle");
        return MBEDTLS_ERR_X509_FATAL_ERROR;
    }

    log_d("%d certificates in bundle", s_crt_bundle.num_certs);

    // Servers may send a trusted root cross-signed by a retired root. The
    // trusted subject alone is insufficient: require its exact SPKI as well.
    // Never clear expiry, hostname, usage or other verification failures.
    const uint8_t *anchor = tls_bundle::find(s_crt_bundle.crts,
        s_crt_bundle.num_certs, child->subject_raw.p, child->subject_raw.len);
    if (depth > 0 && child->ca_istrue &&
        tls_bundle::sameKey(anchor, child->pk_raw.p, child->pk_raw.len)) {
        *flags = 0;
        return 0;
    }
    const uint8_t *parent = tls_bundle::find(s_crt_bundle.crts,
        s_crt_bundle.num_certs, child->issuer_raw.p, child->issuer_raw.len);
    int ret = MBEDTLS_ERR_X509_FATAL_ERROR;
    if (parent) {
        size_t name_len = (size_t(parent[0]) << 8) | parent[1];
        size_t key_len = (size_t(parent[2]) << 8) | parent[3];
        ret = esp_crt_check_signature(child, parent + CRT_HEADER_OFFSET + name_len, key_len);
    }

    if (ret == 0) {
        log_i("Certificate validated");
        *flags = 0;
        return 0;
    }

    log_e("Failed to verify certificate");
    return MBEDTLS_ERR_X509_FATAL_ERROR;
}


/* Initialize the bundle into an array so we can do binary search for certs,
   the bundle generated by the python utility is already presorted by subject name
 */
static esp_err_t esp_crt_bundle_init(const uint8_t *x509_bundle)
{
    s_crt_bundle.num_certs = (x509_bundle[0] << 8) | x509_bundle[1];
    s_crt_bundle.crts = static_cast<const uint8_t **>(calloc(s_crt_bundle.num_certs, sizeof(x509_bundle)));

    if (s_crt_bundle.crts == NULL) {
        log_e("Unable to allocate memory for bundle");
        return ESP_ERR_NO_MEM;
    }

    const uint8_t *cur_crt;
    cur_crt = x509_bundle + BUNDLE_HEADER_OFFSET;

    for (int i = 0; i < s_crt_bundle.num_certs; i++) {
        s_crt_bundle.crts[i] = cur_crt;

        size_t name_len = cur_crt[0] << 8 | cur_crt[1];
        size_t key_len = cur_crt[2] << 8 | cur_crt[3];
        cur_crt = cur_crt + CRT_HEADER_OFFSET + name_len + key_len;
    }

    return ESP_OK;
}

esp_err_t arduino_esp_crt_bundle_attach(void *conf)
{
    esp_err_t ret = ESP_OK;
    // If no bundle has been set by the user then use the bundle embedded in the binary
    if (s_crt_bundle.crts == NULL) {
        log_e("Failed to attach bundle");
        return ESP_ERR_INVALID_STATE;
    }

    if (conf) {
        /* point to a dummy certificate
         * This is only required so that the
         * cacert_ptr passes non-NULL check during handshake
         */
        mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
        mbedtls_x509_crt_init(&s_dummy_crt);
        mbedtls_ssl_conf_ca_chain(ssl_conf, &s_dummy_crt, NULL);
        mbedtls_ssl_conf_verify(ssl_conf, esp_crt_verify_callback, NULL);
    }

    return ret;
}

void arduino_esp_crt_bundle_detach(mbedtls_ssl_config *conf)
{
    free(s_crt_bundle.crts);
    s_crt_bundle.crts = NULL;
    if (conf) {
        mbedtls_ssl_conf_verify(conf, NULL, NULL);
    }
}

void arduino_esp_crt_bundle_set(const uint8_t *x509_bundle)
{
    // Free any previously used bundle
    free(s_crt_bundle.crts);
    s_crt_bundle.crts = NULL;
    esp_crt_bundle_init(x509_bundle);
}
