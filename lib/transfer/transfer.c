#include <app/lib/transfer.h>

static SERIALIZER_DECLARE(register_data_serialize, register_data_t);
inline static SERIALIZER_DECLARE(counter_serialize, uint64_t);

static DESERIALIZER_DECLARE(register_data_deserialize, register_data_t);
inline static DESERIALIZER_DECLARE(counter_deserialize, uint64_t);

transfer_error_t sign_message(struct net_buf_simple *serialized,
                              psa_key_id_t key_id) {
    psa_status_t err;
    struct net_buf_simple hmac;
    size_t hashable_len = serialized->len;

    net_buf_simple_init_with_data(
        &hmac, net_buf_simple_add(serialized, HASH_LEN), HASH_LEN);

    net_buf_simple_reset(&hmac);
    err = crypto_compute_mac(key_id, serialized, hashable_len, &hmac);
    return err == PSA_SUCCESS ? TRANSFER_NO_ERROR
                              : TRANSFER_COULDNT_COMPUTE_MAC;
}

transfer_error_t verify_message(struct net_buf_simple *message,
                                psa_key_id_t key_id, uint64_t counter) {
    psa_status_t err;
    transfer_error_t transfer_err;
    struct net_buf_simple hmac;
    uint64_t remote_counter;
    CRYPTO_MAC_BUF_DEFINE(hmac_self_signed);

    if (message->len < HASH_LEN)
        return TRANSFER_MESSAGE_TO_SHORT;

    net_buf_simple_init_with_data(
        &hmac, net_buf_simple_remove_mem(message, HASH_LEN), HASH_LEN);

    err = crypto_compute_mac(key_id, message, message->len, &hmac_self_signed);
    if (err != PSA_SUCCESS)
        return TRANSFER_COULDNT_COMPUTE_MAC;

    if (memcmp(hmac.data, hmac_self_signed.data, HASH_LEN) != 0)
        return TRANSFER_INVALID_HASH;

    if ((transfer_err = counter_deserialize(&remote_counter, message)) !=
        TRANSFER_NO_ERROR) {
        return transfer_err;
    }

    if (remote_counter < counter)
        return TRANSFER_COUNTER_DIDNT_MATCH;

    return TRANSFER_NO_ERROR;
}

SERIALIZER_DEFINE(advertisment_data_serialize, advertisment_data_t) {
    net_buf_simple_add_u8(result, data->selection_info.num_reg_slots);
    counter_serialize(&data->counter, result);
}

SERIALIZER_DEFINE(subevent_data_with_reg_serialize, subevent_data_t) {
    for (size_t i = 0; i < data->_register_data_count; i++) {
        register_data_serialize(&data->register_data[i], result);
    }
    for (size_t i = 0; i < data->_ack_data_count; i++) {
        net_buf_simple_add_le16(result, data->ack_data[i].ack_id);
    }

    counter_serialize(&data->counter, result);
}

SERIALIZER_DEFINE(subevent_data_serialize, subevent_data_t) {
    for (size_t i = 0; i < data->_ack_data_count; i++) {
        net_buf_simple_add_le16(result, data->ack_data[i].ack_id);
    }
    counter_serialize(&data->counter, result);
}

SERIALIZER_DEFINE(response_data_serialize, response_data_t) {
    net_buf_simple_add_le16(result, data->rsp_metadata.sender_id);
    net_buf_simple_add_mem(result, data->data, sizeof(*data->data));

    counter_serialize(&data->counter, result);
}

DESERIALIZER_DEFINE(advertisement_data_deserialize, advertisment_data_t) {
    DESERIALIZER_SIZE_GUARD(1);
    result->selection_info.num_reg_slots = net_buf_simple_remove_u8(data);
    return 0;
}

DESERIALIZER_DEFINE(subevent_data_with_reg_deserialize, subevent_data_t) {
    transfer_error_t err;
    DESERIALIZER_SIZE_GUARD(2 * result->_ack_data_count);
    for (size_t i = result->_ack_data_count; i > 0; i--) {
        result->ack_data[i - 1].ack_id = net_buf_simple_remove_le16(data);
    }

    for (size_t i = result->_register_data_count; i > 0; i--) {
        if ((err = register_data_deserialize(&result->register_data[i - 1],
                                             data)) != 0)
            return err;
    }
    return 0;
}

DESERIALIZER_DEFINE(subevent_data_deserialize, subevent_data_t) {
    DESERIALIZER_SIZE_GUARD(2 * result->_ack_data_count);
    for (size_t i = result->_ack_data_count; i > 0; i--) {
        result->ack_data[i-1].ack_id = net_buf_simple_remove_le16(data);
    }
    return 0;
}

DESERIALIZER_DEFINE(response_data_deserialize, response_data_t) {
    DESERIALIZER_SIZE_GUARD(sizeof(*result->data) + 2);

    result->data = net_buf_simple_remove_mem(data, sizeof(*result->data));
    result->rsp_metadata.sender_id = net_buf_simple_remove_le16(data);
    return 0;
}

static SERIALIZER_DEFINE(register_data_serialize, register_data_t) {
    net_buf_simple_add_u8(result, data->subevent);
    net_buf_simple_add_u8(result, data->rsp_slot);
}

static SERIALIZER_DEFINE(counter_serialize, uint64_t) {
    net_buf_simple_add_le64(result, *data);
}

static DESERIALIZER_DEFINE(counter_deserialize, uint64_t) {
    DESERIALIZER_SIZE_GUARD(8);
    *result = net_buf_simple_remove_le64(data);
    return 0;
}

static DESERIALIZER_DEFINE(register_data_deserialize, register_data_t) {
    DESERIALIZER_SIZE_GUARD(2);
    result->rsp_slot = net_buf_simple_remove_u8(data);
    result->subevent = net_buf_simple_remove_u8(data);
    return 0;
}
