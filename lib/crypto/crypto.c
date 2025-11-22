#include <app/lib/crypto.h>

int crypto_init() {
    if (psa_crypto_init() != PSA_SUCCESS)
        return -1;
    return 0;
}

int crypto_save_persistent_key(psa_key_id_t persistent_id,
                               struct net_buf_simple key) {
    psa_key_id_t ret_id;
    psa_status_t err;
    psa_key_attributes_t attributes;

    psa_set_key_id(&attributes, persistent_id);
    psa_set_key_lifetime(&attributes, PSA_KEY_LIFETIME_PERSISTENT);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, KEY_BITS);
    psa_set_key_usage_flags(&attributes, KEY_FLAGS);
    psa_set_key_algorithm(&attributes, HMAC_SHA_256);

    err = psa_import_key(&attributes, key.data, key.len, &ret_id);
    if (err != PSA_SUCCESS)
        return -1;
    return 0;
}

int crypto_compute_mac(psa_key_id_t key_id, struct net_buf_simple input,
                       struct net_buf_simple mac_out) {
    size_t out_len;
    psa_status_t err;

    err = psa_mac_compute(key_id, HMAC_SHA_256, input.data, input.len, mac_out.data,
                    mac_out.size, &out_len);

    mac_out.len = out_len;

    if (err != PSA_SUCCESS) {
        return err;
    }
    return 0;
}
