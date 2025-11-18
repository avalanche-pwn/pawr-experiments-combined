#ifndef FREE_LIST_H
#define FREE_LIST_H

#include <app/lib/transfer.h>
#include <stdint.h>


int8_t free_list_append(register_data d);
int8_t free_list_pop(register_data *ret_val);
#endif // FREE_LIST_H
