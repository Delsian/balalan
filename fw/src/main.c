#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static void on_button_changed_handler(uint32_t button_state, uint32_t has_changed)
{
    LOG_DBG("Button pressed");
}

int main(void)
{
    dk_leds_init();
    dk_buttons_init(on_button_changed_handler);

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}