
#if defined(SLAVE_HB_TASK)

static void mb_ipc_task( void *para )
{
	u32 seq = 0;
	bk_err_t send_rc;

	ipc_send_power_up();

	while(1)
	{
		rtos_delay_milliseconds(MB_IPC_HEARTBEAT_TIME);
		seq++;
		bk_printf_raw_nonblock(BK_LOG_INFO, MOD_TAG,
			"[DEBUG-bk-hb] tx-enter seq=%u ms=%u\r\n",
			seq, (u32)rtos_get_time());
		send_rc = ipc_send_heart_beat(0);
		bk_printf_raw_nonblock(BK_LOG_INFO, MOD_TAG,
			"[DEBUG-bk-hb] tx-exit seq=%u rc=%d ms=%u\r\n",
			seq, send_rc, (u32)rtos_get_time());
	}
}

#endif

