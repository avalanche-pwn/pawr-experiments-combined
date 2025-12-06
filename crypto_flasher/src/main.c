#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <app/lib/crypto.h>
#include <app/lib/common.h>
#include "keys.h"

crypto_counter_t counter = {.storage_uid=COUNTER_ID};

BUILD_ASSERT(CONFIG_FLASHED_DEVICE != -1,
             "You need to set id of device to flash");

psa_key_id_t recreate_key(psa_key_id_t id, struct net_buf_simple key) {
    psa_status_t err = psa_destroy_key(id);
    if (err != PSA_SUCCESS && err != PSA_ERROR_INVALID_HANDLE)
        return err;
    return crypto_save_persistent_key(id, &key);
}

int main() {
    k_sleep(K_SECONDS(10));
    if (crypto_init() != 0) {
        printk("Couldn't init crypto");
        goto ret;
    }
    struct net_buf_simple key_buf;
    psa_status_t err;

    printk("Beginning to flash device %d\n", CONFIG_FLASHED_DEVICE);
    err = crypto_secure_counter_init(&counter);
    if (err != PSA_SUCCESS) {
        printk("Failed to init counter %d\n", err);
        goto ret;
    }

    counter.value = 0;
    err = crypto_secure_counter_commit(&counter);
    if (err != PSA_SUCCESS) {
        printk("Failed to commit counter\n");
        goto ret;
    }
    printk("Successfully flashed counter\n");

#if CONFIG_FLASHED_DEVICE == 0
    net_buf_simple_init_with_data(&key_buf, keys[0], KEY_LEN);
    err = recreate_key(ADVERTISER_KEY_ID, key_buf);

    if (err != PSA_SUCCESS)
        goto ret_fail;
    printk("Flashed key 0\n");

    for (size_t i = 1; i < ARRAY_SIZE(keys); i++) {
        net_buf_simple_init_with_data(&key_buf, keys[i], MAC_LEN);
        err = recreate_key(MIN_SCANNER_KEY_ID + i - 1, key_buf);

        if (err != PSA_SUCCESS)
            goto ret_fail;
        printk("Flashed key %d\n", i);
    }
#else  // advertiser part
    net_buf_simple_init_with_data(&key_buf, keys[0], MAC_LEN);
    err = recreate_key(ADVERTISER_KEY_ID, key_buf);
    if (err != PSA_SUCCESS)
        goto ret_fail;

    net_buf_simple_init_with_data(
        &key_buf, keys[CONFIG_FLASHED_DEVICE], MAC_LEN);
    err = recreate_key(MIN_SCANNER_KEY_ID, key_buf);
    if (err != PSA_SUCCESS)
        goto ret_fail;
#endif // scanner part

    printk("Finished flashing device with id %d\n",
           CONFIG_FLASHED_DEVICE);
    k_sleep(K_FOREVER);

ret_fail:
    printk("Failed to flash crypto key, (err: %d)", err);
ret:
    k_sleep(K_FOREVER);
}
