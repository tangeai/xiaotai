/* SDK boundary fixture, copied from Beken bk_avdk_smp commit
 * 398cab28b938e69ba2071a597daf00845dd080b6:
 * ap/middleware/driver/bk7258_ap/gpio_driver.c.
 * Copyright 2020-2021 Beken. Apache-2.0.
 * Hardware register operations are supplied by the host harness.
 */
static const gpio_default_map_t s_default_map[] = GPIO_DEFAULT_DEV_CONFIG;
static const gpio_default_map_t* get_gpio_config(gpio_id_t gpio_id)
{
  for (int i = 0; i < sizeof(s_default_map)/sizeof(gpio_default_map_t); i++)
  {
    if (s_default_map[i].gpio_id == gpio_id)
    {
      return &s_default_map[i];
    }
  }
  return NULL;
}
bk_err_t gpio_dev_unmap(gpio_id_t gpio_id)
{
#if CONFIG_GPIO_DEFAULT_SET_SUPPORT
	const gpio_default_map_t *config = get_gpio_config(gpio_id);

	if (config == NULL)
	{
		GPIO_LOGW("GPIO%d not found in GPIO_DEFAULT_DEV_CONFIG table\r\n",gpio_id);
		return BK_ERR_GPIO_INVALID_OPERATE;
	}

#if 0
    if (config->gpio_time_sharing_multiplex == GPIO_TIME_SHARING_MULTIPLEX_DISABLE)
    {
      GPIO_LOGW("GPIO%d is not set time_sharing_multiplex, please set it in GPIO_DEFAULT_DEV_CONFIG table\r\n",gpio_id);
      return BK_ERR_GPIO_INVALID_OPERATE;
    }
#endif
#endif
	/* Restore a configuration that is not a secondary function to its initial state. */
	gpio_hal_output_enable(&s_gpio.hal, gpio_id, 0);
	gpio_hal_input_enable(&s_gpio.hal, gpio_id, 0);
	gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 0);
	gpio_hal_disable_interrupt(&s_gpio.hal, gpio_id);
	gpio_hal_func_unmap(&s_gpio.hal, gpio_id);

	return BK_OK;
}
