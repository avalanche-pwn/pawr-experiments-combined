#ifndef APP_LIB_DATA_GENERATOR_H
#define APP_LIB_DATA_GENERATOR_H

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/net_buf.h>
#include <zephyr/random/random.h>

/**
 * \brief Struct which is used to configure data generator.
 * After each \ref
 * interval ms a net_buf_simple with \ref len data is generated.  The caller is
 * notified about the generator via \ref generated function.
 */
typedef struct {
    struct net_buf_simple *data;
    int interval;
    void (*init_buf)();
    void (*generated)();
} data_generator_config_t;

/**
 * Initializes data generator
 */
void data_generator_init(data_generator_config_t *config); 
void data_generator_stop();

#endif // APP_LIB_DATA_GENERATOR_H
