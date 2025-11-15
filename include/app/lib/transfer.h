#ifndef APP_LIB_TRANSFER_H
#define APP_LIB_TRANSFER_H
#include <stdint.h>

#define PACKED __attribute__((__packed__))

typedef struct PACKED {
    uint8_t subevent;
    uint8_t rsp_slot;
} subevent_sel_info;

typedef struct PACKED {
    uint16_t sender_id;
} rsp_data;

#endif // APP_LIB_TRANSFER_H
