// Copyright 2025-2026 Beken
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


/* Copyright 2025-2026 Beken. Apache-2.0; fixed release/v3.1.1.10 ring excerpt. */
struct ringbuf
{
    char *p_o;                   /**< Original pointer */
    char *volatile p_r;          /**< Read pointer */
    char *volatile p_w;          /**< Write pointer */
    volatile uint32_t fill_cnt;  /**< Number of filled slots */
    uint32_t size;               /**< Buffer size */
    SemaphoreHandle_t can_read;
    SemaphoreHandle_t can_write;
    SemaphoreHandle_t lock;
    bool abort_read;
    bool abort_write;
    bool is_done_write;         /**< To signal that we are done writing */
    bool unblock_reader_flag;   /**< To unblock instantly from rb_read */
};


typedef struct ringbuf *ringbuf_handle_t;
static bk_err_t rb_abort_read(ringbuf_handle_t rb)
{
    if (rb == NULL)
    {
        return BK_ERR_ADK_INVALID_ARG;
    }
    rb->abort_read = true;
    xSemaphoreGive(rb->can_read);
    return BK_OK;
}
static bk_err_t rb_abort_write(ringbuf_handle_t rb)
{
    if (rb == NULL)
    {
        return BK_ERR_ADK_INVALID_ARG;
    }
    rb->abort_write = true;
    xSemaphoreGive(rb->can_write);
    return BK_OK;
}
bk_err_t rb_abort(ringbuf_handle_t rb)
{
    if (rb == NULL)
    {
        return BK_ERR_ADK_INVALID_ARG;
    }
    bk_err_t err = rb_abort_read(rb);
    err |= rb_abort_write(rb);
    return err;
}
int rb_read(ringbuf_handle_t rb, char *buf, int buf_len, TickType_t ticks_to_wait)
{
    int read_size = 0;
    int total_read_size = 0;
    int ret_val = 0;

    if (rb == NULL)
    {
        return RB_FAIL;
    }

    while (buf_len)
    {
        //take buffer lock
        if (rb_block(rb->lock, portMAX_DELAY) != pdTRUE)
        {
            ret_val = RB_TIMEOUT;
            goto read_err;
        }

        if (rb->fill_cnt < buf_len)
        {
            read_size = rb->fill_cnt;
            /**
             * When non-multiple of 4(word size) bytes are written to I2S, there is noise.
             * Below is the kind of workaround to read only in multiple of 4. Avoids noise when rb is read in small chunks.
             * Note that, when we have buf_len bytes available in rb, we still read those irrespective of if it's multiple of 4.
             */
            read_size = read_size & 0xfffffffc;
            if ((read_size == 0) && rb->is_done_write)
            {
                read_size = rb->fill_cnt;
            }
        }
        else
        {
            read_size = buf_len;
        }

        if (read_size == 0)
        {
            //no data to read, release thread block to allow other threads to write data

            if (rb->is_done_write)
            {
                ret_val = RB_DONE;
                rb_release(rb->lock);
                goto read_err;
            }
            if (rb->abort_read)
            {
                ret_val = RB_ABORT;
                rb_release(rb->lock);
                goto read_err;
            }
            if (rb->unblock_reader_flag)
            {
                //reader_unblock is nothing but forced timeout
                ret_val = RB_TIMEOUT;
                rb_release(rb->lock);
                goto read_err;
            }

            rb_release(rb->lock);
            rb_release(rb->can_write);
            //wait till some data available to read
            if (rb_block(rb->can_read, ticks_to_wait) != pdTRUE)
            {
                ret_val = RB_TIMEOUT;
                goto read_err;
            }
            continue;
        }

        if ((rb->p_r + read_size) > (rb->p_o + rb->size))
        {
            int rlen1 = rb->p_o + rb->size - rb->p_r;
            int rlen2 = read_size - rlen1;
            if (buf)
            {
                os_memcpy(buf, rb->p_r, rlen1);
                os_memcpy(buf + rlen1, rb->p_o, rlen2);
            }
            rb->p_r = rb->p_o + rlen2;
        }
        else
        {
            if (buf)
            {
                os_memcpy(buf, rb->p_r, read_size);
            }
            rb->p_r = rb->p_r + read_size;
        }

        buf_len -= read_size;
        rb->fill_cnt -= read_size;
        total_read_size += read_size;
        buf += read_size;
        rb_release(rb->lock);
        if (buf_len == 0)
        {
            break;
        }
    }
read_err:
    if (total_read_size > 0)
    {
        rb_release(rb->can_write);
    }
    if ((ret_val == RB_FAIL) ||
        (ret_val == RB_ABORT))
    {
        total_read_size = ret_val;
    }
    rb->unblock_reader_flag = false; /* We are anyway unblocking the reader */
    return total_read_size > 0 ? total_read_size : ret_val;
}
bk_err_t rb_destroy(ringbuf_handle_t rb)
{
    if (rb == NULL)
    {
        return BK_ERR_ADK_INVALID_ARG;
    }
    if (rb->p_o)
    {
        audio_free(rb->p_o);
        rb->p_o = NULL;
    }
    if (rb->can_read)
    {
        vSemaphoreDelete(rb->can_read);
        rb->can_read = NULL;
    }
    if (rb->can_write)
    {
        vSemaphoreDelete(rb->can_write);
        rb->can_write = NULL;
    }
    if (rb->lock)
    {
        vSemaphoreDelete(rb->lock);
        rb->lock = NULL;
    }
    audio_free(rb);
    rb = NULL;
    return BK_OK;
}
