// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* Host fixture from bk_avdk_smp 398cab28b938e69ba2071a597daf00845dd080b6,
 * ap/middleware/soc/common/hal/gpio_hal.c. Register writes are harness fakes.
 */
bk_err_t gpio_hal_default_map_init(gpio_hal_t *hal)
{

	const gpio_default_map_t default_map[] = GPIO_DEFAULT_DEV_CONFIG;

	for(int i = 0; i < sizeof(default_map)/sizeof(gpio_default_map_t); i++)
	{
		if (default_map[i].gpio_init == GPIO_INIT_DISABLE)
		{
			continue;
		}
		gpio_hal_output_enable(hal, default_map[i].gpio_id, 0);
		gpio_hal_input_enable(hal, default_map[i].gpio_id, 0);
		gpio_hal_pull_enable(hal, default_map[i].gpio_id, 0);
		gpio_hal_disable_interrupt(hal, default_map[i].gpio_id);

		HAL_LOGV("gpio_id: %d, second_func_en:%d, second_func_dev %d, low_power_io_ctrl:%d",
					default_map[i].gpio_id,
					default_map[i].second_func_en, default_map[i].second_func_dev,
					default_map[i].low_power_io_ctrl);

		HAL_LOGV("int_en: %d, int_type:%d \r\n",
				default_map[i].int_en, default_map[i].int_type);
		
		//function mode
		if(default_map[i].second_func_en) {
			gpio_hal_func_unmap(hal, default_map[i].gpio_id);
			gpio_hal_func_map(hal, default_map[i].gpio_id, default_map[i].second_func_dev);
		}

		//low power
		if (default_map[i].low_power_io_ctrl == GPIO_LOW_POWER_KEEP_OUTPUT_STATUS)
			gpio_hal_output_enable(hal, default_map[i].gpio_id, 1);
		else if (default_map[i].low_power_io_ctrl == GPIO_LOW_POWER_KEEP_INPUT_STATUS)
			gpio_hal_input_enable(hal, default_map[i].gpio_id, 1);

		//io mode
		switch(default_map[i].io_mode)
		{
			case GPIO_IO_DISABLE:
				gpio_hal_output_enable(hal, default_map[i].gpio_id, 0);
				gpio_hal_input_enable(hal, default_map[i].gpio_id, 0);
				break;
			case GPIO_OUTPUT_ENABLE:
				gpio_hal_output_enable(hal, default_map[i].gpio_id, 1);
				gpio_hal_input_enable(hal, default_map[i].gpio_id, 0);
				//NOTES:special combine use it with pull mode
				if(default_map[i].pull_mode == GPIO_PULL_DOWN_EN)
					gpio_hal_set_output_value(hal, default_map[i].gpio_id, 0);
				if(default_map[i].pull_mode == GPIO_PULL_UP_EN)
					gpio_hal_set_output_value(hal, default_map[i].gpio_id, 1);
				break;
			case GPIO_INPUT_ENABLE:
				gpio_hal_output_enable(hal, default_map[i].gpio_id, 0);
				gpio_hal_input_enable(hal, default_map[i].gpio_id, 1);
				break;
			default:
				break;
		}

		//pull mode
		switch(default_map[i].pull_mode)
		{
			case GPIO_PULL_DISABLE:
				gpio_hal_pull_enable(hal, default_map[i].gpio_id, 0);
				break;
			case GPIO_PULL_DOWN_EN:
				gpio_hal_pull_enable(hal, default_map[i].gpio_id, 1);
				gpio_hal_pull_up_enable(hal, default_map[i].gpio_id, 0);
				break;
			case GPIO_PULL_UP_EN:
				gpio_hal_pull_enable(hal, default_map[i].gpio_id, 1);
				gpio_hal_pull_up_enable(hal, default_map[i].gpio_id, 1);
				break;
			default:
				break;
		}

		//interrupt
		if(default_map[i].int_en) {
			gpio_hal_disable_interrupt(hal, default_map[i].gpio_id);	//disable it first to avoid enable IRQ and comes an interrupt at once.
			gpio_hal_set_int_type(hal, default_map[i].gpio_id, default_map[i].int_type);

			for (volatile int i = 0; i < 1000; i++);    //Before enable the interrupt,wait for the internal stability of the chip
			gpio_hal_enable_interrupt(hal, default_map[i].gpio_id);
		} else
			gpio_hal_disable_interrupt(hal, default_map[i].gpio_id);
			
/*BK7258 and BK7256 are different, macros are used to isolate them.*/
#if CONFIG_SOC_BK7236XX
		gpio_hal_clear_chan_interrupt_status(hal,default_map[i].gpio_id);
#endif
		//driver_capacity
		gpio_hal_set_capacity(hal, default_map[i].gpio_id, default_map[i].driver_capacity);
	}

	return BK_OK;	
}
