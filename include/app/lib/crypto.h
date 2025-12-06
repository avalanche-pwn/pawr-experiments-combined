#ifndef APP_LIB_CRYPTO_H
#define APP_LIB_CRYPTO_H

#include <psa/crypto.h>

#include <stdint.h>
#include <zephyr/net_buf.h>
#include <zephyr/psa/key_ids.h>

#define HMAC_SHA_256 PSA_ALG_HMAC(PSA_ALG_SHA_256)
#define HASH_LEN PSA_HASH_LENGTH(HMAC_SHA_256)
#define KEY_BITS 256
#define KEY_LEN (KEY_BITS / 8)
#define KEY_FLAGS PSA_KEY_USAGE_SIGN_MESSAGE
#define MAC_LEN PSA_MAC_LENGTH(PSA_KEY_TYPE_HMAC, KEY_BITS, HMAC_SHA_256)
#define HASH_LEN PSA_HASH_LENGTH(HMAC_SHA_256)
#define COUNTER_DATA_FLAGS PSA_STORAGE_FLAG_NO_CONFIDENTIALITY

#define CRYPTO_KEY_DEFINE_STATIC(name)                                         \
    NET_BUF_SIMPLE_DEFINE_STATIC(name, KEY_BITS / 8)
#define CRYPTO_KEY_DEFINE(name) NET_BUF_SIMPLE_DEFINE(name, KEY_BITS / 8)
#define CRYPTO_MAC_BUF_DEFINE(name) NET_BUF_SIMPLE_DEFINE(name, MAC_LEN)

/**
 * \brief Initialize PSA with psa_crypto_init
 * \return 0 on success negative error on fail
 */
psa_status_t crypto_init();
/**
 * \brief Saves persistent key.
 * \param persistent_id Id with wich the key is saved
 * \param key Should be a securely generated random data of len KEY_BITS
 *
 * \return 0 on success, negative error code otherwise
 */
psa_status_t crypto_save_persistent_key(psa_key_id_t persistent_id,
                               struct net_buf_simple *key);
/**
 * \brief Compute HMAC value and store it in mac_out
 * \param key_id Id of the key to use
 * \param input input to hash
 * \param mac_out Output net buf
 *
 * \return 0 on success, error code otherwise
 */
psa_status_t crypto_compute_mac(psa_key_id_t key_id, struct net_buf_simple *input,
                       size_t hashable_len, struct net_buf_simple *mac_out);

typedef struct {
    uint64_t value;
    psa_key_id_t storage_uid;
} crypto_counter_t;

psa_status_t crypto_secure_counter_init(crypto_counter_t *ctx);
psa_status_t crypto_secure_counter_commit(crypto_counter_t *ctx);

#endif // APP_LIB_CRYPTO_H
