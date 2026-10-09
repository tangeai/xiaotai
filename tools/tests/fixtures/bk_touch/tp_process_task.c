/* Pinned BK v3.1.1.8 task after the existing read-failure recovery patch. */
void tp_process_task(beken_thread_arg_t arg)
{
	int ret;
	tp_data_t tp_data[TP_SUPPORT_MAX_NUM];
	bool touch_active = false;
	uint32_t consecutive_read_failures = 0;

	while (1)
	{
		ret = rtos_get_semaphore(&tp_sema, BEKEN_NEVER_TIMEOUT);
		if(kNoErr != ret)
		{
			LOGE("%s, get semaphore fail!\r\n", __func__);
		}

		os_memset(tp_data, 0x00, sizeof(tp_data));
		if (NULL != current_sensor->read_tp_info)
		{
			int read_rc = BK_FAIL;
			uint32_t attempt = 0;
			for (attempt = 1; attempt <= TP_READ_RETRY_COUNT; attempt++)
			{
				read_rc = current_sensor->read_tp_info(&tp_i2c_cb,
					TP_SUPPORT_MAX_NUM, (void *)tp_data);
				if (BK_OK == read_rc)
				{
					break;
				}
				if (attempt < TP_READ_RETRY_COUNT)
				{
					rtos_delay_milliseconds(TP_READ_RETRY_DELAY_MS);
				}
			}
			if (BK_OK != read_rc)
			{
				consecutive_read_failures++;
				LOGE("%s get tp info fail attempts=%u consecutive=%u!\r\n",
					__func__, (unsigned)attempt - 1U,
					(unsigned)consecutive_read_failures);
				if (consecutive_read_failures >= TP_RECOVERY_FAILURE_COUNT)
				{
					int recovery_rc = tp_recover_sensor();
					LOGW("%s controller recovery rc=%d.\r\n",
						__func__, recovery_rc);
					consecutive_read_failures = 0;
				}
				if (touch_active)
				{
					tp_data[0].event = TP_EVENT_TYPE_UP;
					tp_data[0].x_coordinate = UINT16_MAX;
					tp_data[0].y_coordinate = UINT16_MAX;
					bk_tp_read_info_callback(&tp_data[0]);
					touch_active = false;
					LOGW("%s synthesized release after controller read failure.\r\n",
						 __func__);
				}
			}
			else
			{
				if (attempt > 1U)
				{
					LOGI("%s read recovered after attempt=%u.\r\n",
						__func__, (unsigned)attempt);
				}
				consecutive_read_failures = 0;
				for (uint8_t i=0; i<TP_SUPPORT_MAX_NUM; i++)
				{
					if ((TP_EVENT_TYPE_DOWN == tp_data[i].event) || (TP_EVENT_TYPE_UP == tp_data[i].event) || (TP_EVENT_TYPE_MOVE == tp_data[i].event))
					{
						LOGV("event=%d, track_id=%d, x=%d, y=%d, s=%d, timestamp=%u.\r\n",
									tp_data[i].event,
									tp_data[i].track_id,
									tp_data[i].x_coordinate,
									tp_data[i].y_coordinate,
									tp_data[i].width,
									tp_data[i].timestamp);
					}
					if ((TP_EVENT_TYPE_DOWN == tp_data[i].event) ||
						(TP_EVENT_TYPE_MOVE == tp_data[i].event))
					{
						touch_active = true;
					}
					else if (TP_EVENT_TYPE_UP == tp_data[i].event)
					{
						touch_active = false;
					}

					bk_tp_read_info_callback(&tp_data[i]);
				}
			}
		}

		BK_LOG_ON_ERR(bk_gpio_enable_interrupt(TP_INT_GPIO_ID));
	}
}
