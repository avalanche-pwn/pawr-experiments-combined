#include <stdint.h>
#include <zephyr/kernel.h>
#include <app/lib/transfer.h>

K_MUTEX_DEFINE(free_list_mutex);
static struct {
    register_data_t data[CONFIG_MAX_FREE_SLOTS];
    uint8_t size;
} free_list;

int8_t free_list_append(register_data_t d) {
    int8_t ret = 0;
    k_mutex_lock(&free_list_mutex, K_FOREVER);
    
    if (free_list.size == CONFIG_MAX_FREE_SLOTS) {
        ret = -1;
        goto unlock;
    }
    free_list.data[free_list.size] = d;
    free_list.size++;

unlock:
    k_mutex_unlock(&free_list_mutex);
    return ret;
}

int8_t free_list_pop(register_data_t *ret_val) {
    int8_t ret = 0;;
    k_mutex_lock(&free_list_mutex, K_FOREVER);
    if (free_list.size == 0) {
        ret = -1;
        goto unlock;
    }
    *ret_val = free_list.data[--free_list.size];

unlock:
    k_mutex_unlock(&free_list_mutex);
    return ret;
}
