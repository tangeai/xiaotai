/*
 * Compatibility symbols required by the prebuilt TiRTC library.
 *
 * The BK PSA Mbed TLS component enables the Dubhe SHA-256 implementation.
 * That implementation intentionally does not export the classic incremental
 * mbedtls_sha256_* ABI.  The BK SDK's tg_mbedtls_compat.c reference solves
 * this with a sidecar pool because TiRTC's context layout is not the Dubhe
 * context layout.  Keep the same boundary here; no private Mbed TLS copy is
 * linked by this project.
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <common/bk_assert.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>

#include "dubhe_hash.h"

#define TAG "tirtc_compat"
#define HASH_SLOT_COUNT 8
#define HASH_CONTEXT_MAGIC 0xCAFE5A56u

typedef struct {
    uint32_t magic;
    uint32_t slot;
} hash_context_handle_t;

typedef struct {
    unsigned long opaque[4];
} tirtc_timing_hr_time_t;

typedef struct {
    tirtc_timing_hr_time_t timer;
    uint32_t intermediate_ms;
    uint32_t final_ms;
} tirtc_timing_delay_context_t;

typedef int (*tirtc_rng_fn_t)(void *context, unsigned char *output, size_t size);

static arm_ce_hash_context_t s_hash_contexts[HASH_SLOT_COUNT];
static uint8_t s_hash_used[HASH_SLOT_COUNT];
static beken_mutex_t s_hash_mutex;
static bool s_hash_pool_ready;
typedef struct {
    void *memory;
    void *caller;
} malloc72_record_t;

#define MALLOC72_RECORD_COUNT 64
static malloc72_record_t s_malloc72_records[MALLOC72_RECORD_COUNT];
static uint32_t s_malloc72_next;

/*
 * The BK heap checker reports a corrupted 72-byte allocation as coming from
 * __wrap_malloc, but its report does not include the caller.  Keep the SDK's
 * allocator behaviour exactly and record the caller for that one allocation
 * size. This is deliberately temporary diagnostic code.
 */
void *__wrap_malloc(size_t size)
{
#if CONFIG_PSRAM_AS_SYS_MEMORY
    void *memory = psram_malloc(size);
#else
    void *memory = os_malloc(size);
#endif

    if (size == 72U && memory != NULL) {
        uint32_t slot = s_malloc72_next++ % MALLOC72_RECORD_COUNT;
        s_malloc72_records[slot].memory = memory;
        s_malloc72_records[slot].caller = __builtin_return_address(0);
    }
    return memory;
}

void __wrap_free(void *memory)
{
    if (memory != NULL) {
        for (uint32_t slot = 0; slot < MALLOC72_RECORD_COUNT; ++slot) {
            if (s_malloc72_records[slot].memory != memory) continue;
            if (((const uint8_t *)memory)[72] != 0xcdU) {
                BK_DUMP_OUT("MALLOC72_OVERFLOW ptr=%p byte=0x%02x alloc=%p free=%p\r\n",
                            memory, ((const uint8_t *)memory)[72],
                            s_malloc72_records[slot].caller,
                            __builtin_return_address(0));
            }
            s_malloc72_records[slot].memory = NULL;
            break;
        }
    }

#if CONFIG_PSRAM_AS_SYS_MEMORY
    psram_free(memory);
#else
    os_free(memory);
#endif
}

static int hash_pool_init(void)
{
    if (s_hash_pool_ready) return BK_OK;
    if (rtos_init_mutex(&s_hash_mutex) != BK_OK) return BK_FAIL;
    s_hash_pool_ready = true;
    return BK_OK;
}

static int hash_slot_alloc(void)
{
    int slot = -1;

    if (hash_pool_init() != BK_OK) return -1;
    rtos_lock_mutex(&s_hash_mutex);
    for (int i = 0; i < HASH_SLOT_COUNT; ++i) {
        if (!s_hash_used[i]) {
            s_hash_used[i] = 1;
            slot = i;
            break;
        }
    }
    rtos_unlock_mutex(&s_hash_mutex);
    return slot;
}

static void hash_slot_free(uint32_t slot)
{
    if (slot >= HASH_SLOT_COUNT || !s_hash_pool_ready) return;
    rtos_lock_mutex(&s_hash_mutex);
    s_hash_used[slot] = 0;
    rtos_unlock_mutex(&s_hash_mutex);
}

void mbedtls_sha256_init(void *context)
{
    hash_context_handle_t *handle = context;
    int slot = hash_slot_alloc();

    if (slot < 0 || arm_ce_hash_init(&s_hash_contexts[slot]) != BK_OK) {
        if (slot >= 0) hash_slot_free((uint32_t)slot);
        handle->magic = 0;
        BK_LOGE(TAG, "SHA-256 context allocation failed\n");
        return;
    }
    handle->magic = HASH_CONTEXT_MAGIC;
    handle->slot = (uint32_t)slot;
}

int mbedtls_sha256_starts(void *context, int is224)
{
    hash_context_handle_t *handle = context;
    arm_ce_hash_mode_t mode = is224 ? ARM_HASH_MODE_SHA224 : ARM_HASH_MODE_SHA256;

    if (handle->magic != HASH_CONTEXT_MAGIC || handle->slot >= HASH_SLOT_COUNT) return -1;
    return arm_ce_hash_start(&s_hash_contexts[handle->slot],
                             ARM_HASH_VECT_DEFAULT, 0, NULL, 0, mode);
}

int mbedtls_sha256_update(void *context, const unsigned char *input, size_t length)
{
    hash_context_handle_t *handle = context;

    if (handle->magic != HASH_CONTEXT_MAGIC || handle->slot >= HASH_SLOT_COUNT) return -1;
    return arm_ce_hash_update(&s_hash_contexts[handle->slot], input, length);
}

int mbedtls_sha256_finish(void *context, unsigned char *output)
{
    hash_context_handle_t *handle = context;

    if (handle->magic != HASH_CONTEXT_MAGIC || handle->slot >= HASH_SLOT_COUNT) return -1;
    return arm_ce_hash_finish(&s_hash_contexts[handle->slot], output);
}

void mbedtls_sha256_free(void *context)
{
    hash_context_handle_t *handle = context;

    if (handle->magic != HASH_CONTEXT_MAGIC) return;
    hash_slot_free(handle->slot);
    handle->magic = 0;
}

/* Optional TiRTC hook. TiRTC has already generated the key when it calls it. */
void webrtc_init_prev_create_key(void *key, tirtc_rng_fn_t rng, void *rng_context)
{
    (void)key;
    (void)rng;
    (void)rng_context;
}

/* Full BK Mbed TLS disables MBEDTLS_DEBUG_C, but TiRTC treats this as optional. */
void mbedtls_debug_set_threshold(int threshold)
{
    (void)threshold;
}

/* TiRTC v2.5 uses the classic 32-bit timing context layout. */
void mbedtls_timing_set_delay(void *data, uint32_t intermediate_ms, uint32_t final_ms)
{
    tirtc_timing_delay_context_t *context = data;
    uint32_t now;

    context->intermediate_ms = intermediate_ms;
    context->final_ms = final_ms;
    if (final_ms == 0) return;
    now = rtos_get_time();
    __builtin_memcpy(context->timer.opaque, &now, sizeof(now));
}

int mbedtls_timing_get_delay(void *data)
{
    tirtc_timing_delay_context_t *context = data;
    uint32_t start = 0;
    uint32_t elapsed;

    if (context->final_ms == 0) return -1;
    __builtin_memcpy(&start, context->timer.opaque, sizeof(start));
    elapsed = rtos_get_time() - start;
    if (elapsed >= context->final_ms) return 2;
    if (elapsed >= context->intermediate_ms) return 1;
    return 0;
}

/*
 * This is inline-only in BK's Mbed TLS 3 headers but an exported function in
 * the ABI used to build TiRTC. The state field is the first ABI-relevant
 * member used here; value 16 is MBEDTLS_SSL_HANDSHAKE_OVER in Mbed TLS 2.x.
 */
int mbedtls_ssl_is_handshake_over(void *ssl)
{
    typedef struct {
        const void *config;
        int state;
    } ssl_context_head_t;
    const ssl_context_head_t *context = ssl;

    /* MBEDTLS_SSL_HANDSHAKE_OVER in the BK SDK's Mbed TLS 3.5 state enum. */
    return context != NULL && context->state >= 27;
}
