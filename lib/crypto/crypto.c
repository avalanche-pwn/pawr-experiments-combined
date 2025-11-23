#include <app/lib/crypto.h>

#define BIT_SIZEOF(type) sizeof(type) * BITS_PER_BYTE

inline psa_status_t crypto_init() { return psa_crypto_init(); }

psa_status_t crypto_save_persistent_key(psa_key_id_t persistent_id,
                                        struct net_buf_simple *key) {
    psa_key_id_t ret_id;
    psa_key_attributes_t attributes;

    psa_set_key_id(&attributes, persistent_id);
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_PERSISTENT);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, KEY_BITS);
    psa_set_key_usage_flags(&attributes, KEY_FLAGS);
    psa_set_key_algorithm(&attributes, HMAC_SHA_256);

    return psa_import_key(&attributes, key->data, key->len, &ret_id);
}

int crypto_compute_mac(psa_key_id_t key_id, struct net_buf_simple *input,
                       size_t hashable_len, struct net_buf_simple *mac_out) {
    size_t out_len;
    uint8_t *mac_data = net_buf_simple_add(mac_out, MAC_LEN);

    return psa_mac_compute(key_id, HMAC_SHA_256, input->data, hashable_len,
                           mac_data, MAC_LEN, &out_len);
}

psa_status_t crypto_secure_counter_init(crypto_counter_t *ctx) {
    psa_status_t err;
    size_t read;
    uint8_t tmp[sizeof(ctx->value)];
    err = psa_export_key(ctx->storage_uid, tmp, sizeof(ctx->value), &read);
    memcpy(&ctx->value, tmp, read);

    if (err == PSA_ERROR_INVALID_HANDLE) {
        psa_generate_random(tmp, sizeof(ctx->value));
        for (size_t i = 0; i < sizeof(ctx->value); i++) {
            ctx->value |= tmp[i] << i * 8;
        }
        return crypto_secure_counter_commit(ctx);
    }
    return err;
}

inline psa_status_t crypto_secure_counter_commit(crypto_counter_t *ctx) {
    psa_key_id_t ret_id;
    psa_key_attributes_t attributes;
    psa_status_t err;

    err = psa_destroy_key(ctx->storage_uid);
    if (err != PSA_SUCCESS && err != PSA_ERROR_INVALID_HANDLE)
        return err;

    psa_set_key_id(&attributes, ctx->storage_uid);
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_PERSISTENT);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RAW_DATA);
    psa_set_key_bits(&attributes, BIT_SIZEOF(ctx->storage_uid));
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);

    return psa_import_key(&attributes, (uint8_t *)&ctx->value,
                          sizeof(ctx->storage_uid), &ret_id);
}
