#ifndef APP_LIB_TRANSFER_H
#define APP_LIB_TRANSFER_H
#include <stdint.h>
#include <app/lib/crypto.h>

#define PACKED __attribute__((__packed__))

typedef struct PACKED {
    uint8_t num_reg_slots;
} subevent_sel_info_t;

typedef struct PACKED {
    uint16_t sender_id;
} rsp_data_t;

typedef enum PACKED {
    REGISTER_DATA,
    ACK_DATA
} data_t;

typedef struct PACKED {
    uint8_t subevent;
    uint8_t rsp_slot;
} register_data_t;

typedef struct PACKED {
    uint16_t ack_id;
} ack_data_t;

typedef struct PACKED {
    uint64_t counter;
    uint8_t hmac[PSA_HASH_LENGTH(HMAC_SHA_256)];
} subevent_sign_t;

#endif // APP_LIB_TRANSFER_H
