bk_err_t rb_abort_write(ringbuf_handle_t rb)
{
    if (rb == NULL)
    {
        return RB_FAIL;
    }
    rb->abort_write = true;
    xSemaphoreGive(rb->can_write);
    return RB_OK;
}
