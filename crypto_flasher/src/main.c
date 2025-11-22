#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <app/lib/crypto.h>

uint8_t keys[3][KEY_LEN] = {
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
    "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC",
};
const psa_key_type_t advertiser_key_id =
    PSA_KEY_ID_USER_MIN + CONFIG_ADVERTISER_KEY_ID_OFFSET;
const psa_key_type_t min_scanner_key_id =
    advertiser_key_id + CONFIG_SCANNER_MIN_KEY_ID;

BUILD_ASSERT(CONFIG_FLASHED_DEVICE != -1,
             "You need to set id of device to flash");

psa_key_id_t recreate_key(psa_key_id_t id, struct net_buf_simple key) {
    psa_status_t err = psa_destroy_key(id);
    if (err != PSA_SUCCESS && err != PSA_ERROR_INVALID_HANDLE)
        return err;
    return crypto_save_persistent_key(id, key);
}

int main() {
    k_sleep(K_SECONDS(10));
    if (crypto_init() != 0) {
        printk("Couldn't init crypto");
    }
    struct net_buf_simple key_buf;
    psa_status_t err;

    printk("Beginning to flash device %d\n", CONFIG_FLASHED_DEVICE);
#if CONFIG_FLASHED_DEVICE == 0
    net_buf_simple_init_with_data(&key_buf, keys[0], KEY_LEN);
    err = recreate_key(advertiser_key_id, key_buf);

    if (err != PSA_SUCCESS)
        goto ret_fail;
    printk("Flashed key 0\n");

    for (size_t i = 1; i < ARRAY_SIZE(keys); i++) {
        net_buf_simple_init_with_data(&key_buf, keys[i], MAC_LEN);
        err = recreate_key(min_scanner_key_id + i - 1, key_buf);

        if (err != PSA_SUCCESS)
            goto ret_fail;
        printk("Flashed key %d\n", i);
    }
#else  // advertiser part
    net_buf_simple_init_with_data(&key_buf, keys[0], MAC_LEN);
    err = recreate_key(advertiser_key_id, key_buf);
    if (err != PSA_SUCCESS)
        goto ret_fail;

    net_buf_simple_init_with_data(
        &key_buf, keys[CONFIG_FLASHED_DEVICE], MAC_LEN);
    err = recreate_key(min_scanner_key_id, key_buf);
    if (err != PSA_SUCCESS)
        goto ret_fail;
#endif // scanner part

    printk("Finished flashing device with id %d\n",
           CONFIG_FLASHED_DEVICE);
    k_sleep(K_FOREVER);

ret_fail:
    printk("Failed to flash crypto key, (err: %d)", err);
    k_sleep(K_FOREVER);
}
