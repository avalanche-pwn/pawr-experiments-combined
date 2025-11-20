#ifndef APP_LIB_CRYPTO_H
#define APP_LIB_CRYPTO_H

#include <psa/crypto.h>
#include <stdint.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/psa/key_ids.h>

#define HMAC_SHA_256 PSA_ALG_HMAC(PSA_ALG_SHA_256)
#define HASH_LEN PSA_HASH_LENGTH(HMAC_SHA_256)
#define KEY_BITS 256
#define KEY_FLAGS PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH

int crypto_init();
int crypto_save_persistent_key(psa_key_id_t persistent_id, struct net_buf_simple key);

#endif // APP_LIB_CRYPTO_H
