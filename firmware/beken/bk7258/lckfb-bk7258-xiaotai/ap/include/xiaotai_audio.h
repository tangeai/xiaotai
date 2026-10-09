#ifndef XIAOTAI_AUDIO_H
#define XIAOTAI_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TiRTC product audio contracts. Remote-view/call/VoIP use G.711 A-law at
 * 8 kHz in 20 ms packets; AI uses Opus at 16 kHz in 20 ms packets. */

typedef enum {
    XIAOTAI_AUDIO_CODEC_ALAW_8K = 0,
    XIAOTAI_AUDIO_CODEC_OPUS_16K,
} xiaotai_audio_codec_t;

typedef int (*xiaotai_audio_uplink_fn)(const uint8_t *alaw,
                                       size_t size,
                                       uint32_t timestamp_ms,
                                       void *context);

/** Start full-duplex board audio. The callback runs on the microphone task. */
int xiaotai_audio_start(xiaotai_audio_codec_t codec,
                        xiaotai_audio_uplink_fn uplink, void *context);

/**
 * Queue received G.711 A-law without blocking the TiRTC callback.
 * Returns the number of bytes accepted, or a negative error code.
 */
int xiaotai_audio_play_alaw(const uint8_t *data, size_t size);
int xiaotai_audio_play_opus(const uint8_t *data, size_t size);

int xiaotai_audio_stop(void);
bool xiaotai_audio_running(void);
/** True after queued playback is empty and no downlink arrived during quiet_ms. */
bool xiaotai_audio_playback_is_drained(uint32_t quiet_ms);
/** Log the bounded AI receive queue and estimated playback tail. */
void xiaotai_audio_log_playback_status(const char *stage);

/** Runtime product controls. Values are retained across audio restarts. */
void xiaotai_audio_set_volume(unsigned level);
unsigned xiaotai_audio_volume(void);
void xiaotai_audio_set_speaker_muted(bool muted);
void xiaotai_audio_set_microphone_muted(bool muted);
/** Set persistent 1..5 capture sensitivity; returns a board adapter status. */
int xiaotai_audio_set_microphone_sensitivity(unsigned sensitivity);
void xiaotai_audio_set_uplink_enabled(bool enabled);
bool xiaotai_audio_uplink_enabled(void);

/**
 * Play server-generated 8 kHz mono signed 16-bit PCM three times.
 * The buffer must come from psram_malloc(). Ownership transfers on success;
 * the caller retains ownership when an error is returned.
 */
int xiaotai_audio_announce_verification_pcm(int16_t *pcm,
                                            size_t sample_count);
void xiaotai_audio_cancel_prompt(void);

#endif
