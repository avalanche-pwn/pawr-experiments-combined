#ifndef FREE_LIST_H
#define FREE_LIST_H

#include <app/lib/transfer.h>
#include <stdint.h>


int8_t free_list_append(register_data_t d);
int8_t free_list_pop(register_data_t *ret_val);
#endif // FREE_LIST_H
