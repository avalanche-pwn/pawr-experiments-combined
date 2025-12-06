#ifndef APP_LIB_COMMON_H
#define APP_LIB_COMMON_H

#include <psa/crypto.h>
#include <stdint.h>
#include <zephyr/psa/key_ids.h>

#define STORAGE_MASK 0xff0000
typedef struct {
    uint8_t subevent;
    uint8_t rsp_slot;
} pawr_timing;

#define ADVERTISER_KEY_ID (PSA_KEY_ID_USER_MIN + CONFIG_ADVERTISER_KEY_ID_OFFSET)
#define MIN_SCANNER_KEY_ID (ADVERTISER_KEY_ID + CONFIG_SCANNER_MIN_KEY_ID)
#define COUNTER_ID (PSA_KEY_ID_USER_MIN | STORAGE_MASK)


#endif // APP_LIB_COMMON_H
