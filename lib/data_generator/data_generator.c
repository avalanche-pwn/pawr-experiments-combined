#include <app/lib/data_generator.h>

static void generate_data(struct k_timer *timer);
static void cleanup_fn(struct k_timer *timer);

K_TIMER_DEFINE(data_gen_timer, &generate_data, NULL);
static data_generator_config_t *config_p;


void data_generator_init(data_generator_config_t *config) {
    config_p = config;
    k_timer_start(&data_gen_timer, K_SECONDS(config->interval), K_SECONDS(config->interval));
}

void data_generator_stop() {
    k_timer_stop(&data_gen_timer);
}

static void generate_data(struct k_timer *timer){
    uint8_t data[config_p->data->len];
    sys_rand_get(&data, config_p->data->len);
    net_buf_simple_reset(config_p->data);

    if (config_p->init_buf)
        config_p->init_buf();

    net_buf_simple_add_mem(config_p->data, &data, config_p->data->len);

    if (config_p->generated)
        config_p->generated();
};

