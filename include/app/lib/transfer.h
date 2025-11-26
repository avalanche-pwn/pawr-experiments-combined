#ifndef APP_LIB_TRANSFER_H
#define APP_LIB_TRANSFER_H
#include <zephyr/net_buf.h>

#include <app/lib/crypto.h>
#include <stdint.h>

#define PACKED __attribute__((__packed__))

#define UNUSED_DATA_LEN 244 - HASH_LEN - sizeof(uint64_t) - sizeof(rsp_data_t) - sizeof(uint8_t)

#define SERIALIZER_DECLARE(name, type)                                         \
    void name(type *data, struct net_buf_simple *result);
#define SERIALIZER_DEFINE(name, type)                                          \
    void name(type *data, struct net_buf_simple *result)

#define DESERIALIZER_DECLARE(name, type)                                       \
    transfer_error_t name(type *result, struct net_buf_simple *data);
#define DESERIALIZER_DEFINE(name, type)                                        \
    transfer_error_t name(type *result, struct net_buf_simple *data)

#define DESERIALIZER_SIZE_GUARD(size)                                          \
    if (data->len < size)                                                      \
        return TRANSFER_MESSAGE_TO_SHORT;

typedef struct PACKED {
    uint8_t num_reg_slots;
} subevent_sel_info_t;

typedef struct PACKED {
    uint16_t sender_id;
} rsp_data_t;

typedef enum PACKED { REGISTER_DATA, ACK_DATA } data_t;

typedef struct PACKED {
    uint8_t subevent;
    uint8_t rsp_slot;
} register_data_t;

typedef struct PACKED {
    uint16_t ack_id;
} ack_data_t;

typedef struct {
    subevent_sel_info_t selection_info;
    uint64_t counter;
} advertisment_data_t;

typedef struct {
    register_data_t *register_data;
    ack_data_t *ack_data;
    uint64_t counter;
    size_t _register_data_count;
    size_t _ack_data_count;
} subevent_data_t;

typedef struct {
    rsp_data_t rsp_metadata;
    uint8_t *data;
    uint8_t data_len;
    uint64_t counter;
} response_data_t;

typedef enum {
    TRANSFER_NO_ERROR,
    TRANSFER_COULDNT_COMPUTE_MAC,
    TRANSFER_MESSAGE_TO_SHORT,
    TRANSFER_INVALID_HASH,
    TRANSFER_COUNTER_DIDNT_MATCH
} transfer_error_t;

transfer_error_t sign_message(struct net_buf_simple *serialized,
                              psa_key_id_t key_id);
transfer_error_t verify_message(struct net_buf_simple *message,
                                psa_key_id_t key_id, uint64_t *counter);

SERIALIZER_DECLARE(advertisment_data_serialize, advertisment_data_t);
SERIALIZER_DECLARE(subevent_data_with_reg_serialize, subevent_data_t);
SERIALIZER_DECLARE(subevent_data_serialize, subevent_data_t);
SERIALIZER_DECLARE(response_data_serialize, response_data_t);

DESERIALIZER_DECLARE(advertisement_data_deserialize, advertisment_data_t);
DESERIALIZER_DECLARE(subevent_data_with_reg_deserialize, subevent_data_t);
DESERIALIZER_DECLARE(subevent_data_deserialize, subevent_data_t);
DESERIALIZER_DECLARE(response_data_deserialize, response_data_t);

#endif // APP_LIB_TRANSFER_H
