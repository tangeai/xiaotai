#ifndef XIAOTAI_H5_FANOUT_API_H
#define XIAOTAI_H5_FANOUT_API_H

/* Included after tiRTC.h. The implementation is instantiated once by each
 * platform adapter, alongside its existing single-owner lifecycle callbacks.
 * SDK calls stay outside the platform critical section. */
#define XIAOTAI_H5_VIEWERS 4U
static int h5_fanout_send_audio(tirtc_conn_t, const TIRTCFRAMEINFO *, const void *);
static int h5_fanout_send_video(tirtc_conn_t, const TIRTCFRAMEINFO *, const void *);
static int h5_fanout_disconnect(tirtc_conn_t);
static int h5_fanout_subscribe_audio(tirtc_conn_t, uint8_t);
static size_t h5_fanout_buffer_used(tirtc_conn_t);

/* Function-like macros leave (TiRtcSendAudioStream)(...) available to the
 * implementation for a real SDK call, without recursively routing it. */
#define TiRtcSendAudioStream(c,f,d) h5_fanout_send_audio((c),(f),(d))
#define TiRtcSendVideoStream(c,f,d) h5_fanout_send_video((c),(f),(d))
#define TiRtcSubscribeAudio(c,id) h5_fanout_subscribe_audio((c),(id))
#define TiRtcDisconnect(c) h5_fanout_disconnect((c))
#define TiRtcGetSendBufferUsed(c) h5_fanout_buffer_used((c))
#endif
