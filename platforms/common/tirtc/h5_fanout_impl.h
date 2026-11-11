/* Instantiated by the TiRTC adapter after its legacy callbacks. Four viewers
 * share one capture/encoder and one product session generation. The oldest
 * live viewer alone owns talkback/commands. All metadata needs the adapter's
 * bounded critical section; no SDK or product callback runs under it.
 * Each SDK call borrows a peer reference. A closing peer cannot be borrowed,
 * and TiRtcDisconnect is deferred until its final call returns. */
typedef struct {
    tirtc_conn_t connection;
    uint32_t order;
    unsigned references;
    bool audio, video, closing, disconnecting, needs_key;
} h5_fanout_peer_t;
static h5_fanout_peer_t s_h5_peers[XIAOTAI_H5_VIEWERS];
static uint32_t s_h5_order;
static bool s_h5_stopping;
static uint32_t s_h5_key_requested_ms;
static bool s_h5_key_requested;
static void h5_fanout_service(void);
static void h5_fanout_on_disconnected(tirtc_conn_t connection);

static h5_fanout_peer_t *h5_fanout_find(tirtc_conn_t connection)
{
    if (!connection) return NULL;
    for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i)
        if (s_h5_peers[i].connection == connection) return &s_h5_peers[i];
    return NULL;
}

static bool h5_fanout_any(bool video)
{
    for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i)
        if (s_h5_peers[i].connection && !s_h5_peers[i].closing &&
            (video ? s_h5_peers[i].video : s_h5_peers[i].audio)) return true;
    return false;
}

static void h5_fanout_release(tirtc_conn_t connection)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *peer = h5_fanout_find(connection);
    if (peer && peer->references) --peer->references;
    h5_fanout_unlock(token);
    h5_fanout_service();
}

static void h5_fanout_service(void)
{
    tirtc_conn_t next_owner = NULL;
    tirtc_conn_t stop_video = NULL;
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *oldest = NULL;
    if (H5_FANOUT_ACTIVE() && !s_h5_stopping) {
        for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i) {
            h5_fanout_peer_t *p = &s_h5_peers[i];
            if (p->connection && !p->closing &&
                (!oldest || (int32_t)(p->order - oldest->order) < 0)) oldest = p;
        }
        if (oldest && atomic_load(&s_connection) != (uintptr_t)oldest->connection) {
            next_owner = oldest->connection;
            ++oldest->references;
            atomic_store(&s_connection, (uintptr_t)next_owner);
        }
        bool video = h5_fanout_any(true);
        if (oldest && atomic_load(&s_video_subscribed) && !video)
            stop_video = oldest->connection;
        atomic_store(&s_audio_subscribed, h5_fanout_any(false));
        atomic_store(&s_video_subscribed, video);
    }
    h5_fanout_unlock(token);
    if (H5_FANOUT_ACTIVE()) H5_FANOUT_UPLINK_CHANGED();
    if (stop_video) on_unsubscribe_video(stop_video, H5_FANOUT_VIDEO_ID);
    if (next_owner) {
        /* Device subscribes only to the new controller's talkback stream.
         * Old controller audio is already rejected by the primary-handle gate. */
        int result = (TiRtcSubscribeAudio)(next_owner, H5_FANOUT_DOWN_AUDIO_ID);
        token = h5_fanout_lock();
        h5_fanout_peer_t *p = h5_fanout_find(next_owner);
        if (p) {
            --p->references;
            if (result < 0) p->closing = true;
        }
        h5_fanout_unlock(token);
        if (result < 0) h5_fanout_service();
    }
    for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i) {
        token = h5_fanout_lock();
        h5_fanout_peer_t *p = &s_h5_peers[i];
        tirtc_conn_t close = NULL;
        if (p->connection && p->closing && !p->disconnecting && !p->references) {
            p->disconnecting = true;
            close = p->connection;
        }
        h5_fanout_unlock(token);
        if (close) {
            int result = (TiRtcDisconnect)(close);
            if (result == TIRTC_E_INVALID_HANDLE) h5_fanout_on_disconnected(close);
            else if (result < 0) {
                token = h5_fanout_lock();
                p = h5_fanout_find(close);
                if (p) p->disconnecting = false;
                h5_fanout_unlock(token);
            }
        }
    }
}

static void h5_fanout_on_conn_accepted(tirtc_conn_t connection)
{
    unsigned token = h5_fanout_lock();
    unsigned count = 0, vacant = XIAOTAI_H5_VIEWERS;
    for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i) {
        if (s_h5_peers[i].connection) ++count;
        else vacant = i;
    }
    bool duplicate = h5_fanout_find(connection) != NULL;
    bool additional = count && H5_FANOUT_ACTIVE() && H5_FANOUT_ACCEPTING() &&
                      !s_h5_stopping && vacant < XIAOTAI_H5_VIEWERS;
    if (additional && !duplicate) s_h5_peers[vacant] = (h5_fanout_peer_t){
        .connection = connection, .order = ++s_h5_order, .needs_key = true};
    h5_fanout_unlock(token);
    if (duplicate || additional) return;
    if (count) { (void)(TiRtcDisconnect)(connection); return; }
    on_conn_accepted(connection);
    token = h5_fanout_lock();
    if (H5_FANOUT_ACTIVE() && atomic_load(&s_connection) == (uintptr_t)connection) {
        s_h5_stopping = false;
        s_h5_order = 1;
        s_h5_key_requested = false;
        s_h5_peers[0] = (h5_fanout_peer_t){.connection=connection, .order=1, .needs_key=true};
    }
    h5_fanout_unlock(token);
}

static void h5_fanout_on_disconnected(tirtc_conn_t connection)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *p = h5_fanout_find(connection);
    bool grouped = p != NULL;
    if (p) *p = (h5_fanout_peer_t){0};
    bool remaining = false;
    for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i)
        if (s_h5_peers[i].connection && !s_h5_peers[i].closing) remaining = true;
    h5_fanout_unlock(token);
    if (grouped && remaining) { h5_fanout_service(); return; }
    /* Do not admit a replacement group between the SDK close callback and
     * the runtime processing the old group's queued teardown event. */
    if (grouped && H5_FANOUT_ACTIVE() && atomic_load(&s_connection) == (uintptr_t)connection)
        H5_FANOUT_LAST_CLOSED();
    on_disconnected(connection);
}

static void h5_fanout_on_conn_error(tirtc_conn_t connection, int error)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *p = h5_fanout_find(connection);
    bool grouped = p != NULL;
    if (p) p->closing = true;
    h5_fanout_unlock(token);
    if (grouped) h5_fanout_service();
    else on_conn_error(connection, error);
}

static int h5_fanout_disconnect(tirtc_conn_t connection)
{
    unsigned token = h5_fanout_lock();
    bool grouped = h5_fanout_find(connection) != NULL;
    if (grouped) {
        s_h5_stopping = true;
        for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i)
            if (s_h5_peers[i].connection) s_h5_peers[i].closing = true;
    }
    h5_fanout_unlock(token);
    if (!grouped) return (TiRtcDisconnect)(connection);
    h5_fanout_service();
    return 0;
}

static int h5_fanout_subscription(tirtc_conn_t connection, uint8_t stream_id,
                                  bool video, bool enable)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *p = h5_fanout_find(connection);
    bool grouped = p != NULL;
    bool accepted = p && !p->closing &&
        stream_id == (video ? H5_FANOUT_VIDEO_ID : H5_FANOUT_AUDIO_ID);
    tirtc_conn_t primary = (tirtc_conn_t)atomic_load(&s_connection);
    if (accepted) {
        if (video) { p->video = enable; if (enable) p->needs_key = true; }
        else p->audio = enable;
    }
    bool any = h5_fanout_any(video);
    h5_fanout_unlock(token);
    if (!grouped) return -2; /* legacy AI/call paths retain their own contract */
    if (!accepted) return -1;
    if (enable) {
        int result = video ? on_subscribe_video(primary, stream_id) : on_subscribe_audio(primary, stream_id);
        if (video && result >= 0) on_request_key_frame(primary, stream_id);
        return result;
    }
    if (!any) {
        if (video) on_unsubscribe_video(primary, stream_id);
        else on_unsubscribe_audio(primary, stream_id);
    }
    return 0;
}
static int h5_fanout_on_subscribe_audio(tirtc_conn_t c, uint8_t id)
{
    int result = h5_fanout_subscription(c,id,false,true);
    return result == -2 ? on_subscribe_audio(c,id) : result;
}
static int h5_fanout_on_subscribe_video(tirtc_conn_t c, uint8_t id)
{
    int result = h5_fanout_subscription(c,id,true,true);
    return result == -2 ? on_subscribe_video(c,id) : result;
}
static void h5_fanout_on_unsubscribe_audio(tirtc_conn_t c, uint8_t id)
{
    if (h5_fanout_subscription(c,id,false,false) == -2) on_unsubscribe_audio(c,id);
}
static void h5_fanout_on_unsubscribe_video(tirtc_conn_t c, uint8_t id)
{
    if (h5_fanout_subscription(c,id,true,false) == -2) on_unsubscribe_video(c,id);
}
static void h5_fanout_on_request_key_frame(tirtc_conn_t c, uint8_t id)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *p = h5_fanout_find(c);
    tirtc_conn_t primary = p && !p->closing ? (tirtc_conn_t)atomic_load(&s_connection) : c;
    h5_fanout_unlock(token);
    on_request_key_frame(primary,id);
}
static void h5_fanout_on_audio(tirtc_conn_t c, const TIRTCFRAMEINFO *f, void *data)
{
    unsigned token = h5_fanout_lock();
    h5_fanout_peer_t *p = h5_fanout_find(c);
    bool allowed = atomic_load(&s_connection) == (uintptr_t)c && (!p || !p->closing);
    h5_fanout_unlock(token);
    if (allowed) on_audio(c,f,data);
}
static void h5_fanout_on_command(tirtc_conn_t c, uint32_t command, const void *data, uint32_t size)
{
    if (atomic_load(&s_connection) == (uintptr_t)c) on_command(c,command,data,size);
}

static int h5_fanout_send(tirtc_conn_t connection, const TIRTCFRAMEINFO *frame,
                          const void *data, bool video)
{
    tirtc_conn_t peers[XIAOTAI_H5_VIEWERS];
    unsigned count = 0;
    bool request_key = false;
    unsigned token = h5_fanout_lock();
    bool grouped = H5_FANOUT_ACTIVE();
    if (grouped && !s_h5_stopping) {
        for (unsigned i = 0; i < XIAOTAI_H5_VIEWERS; ++i) {
            h5_fanout_peer_t *p = &s_h5_peers[i];
            if (p->connection && !p->closing && (video ? p->video : p->audio)) {
                if (video && frame->media == TIRTC_VIDEO_H264 && p->needs_key &&
                    !(frame->flags & TIRTC_FRAME_FLAG_KEY_FRAME)) { request_key = true; continue; }
                ++p->references;
                peers[count++] = p->connection;
            }
        }
    }
    h5_fanout_unlock(token);
    if (!grouped) {
        if (!connection || atomic_load(&s_connection) != (uintptr_t)connection)
            return TIRTC_E_INVALID_HANDLE;
        return video ? (TiRtcSendVideoStream)(connection,frame,data) :
                       (TiRtcSendAudioStream)(connection,frame,data);
    }
    int result = TIRTC_E_BUSY;
    for (unsigned i = 0; i < count; ++i) {
        int sent = TIRTC_E_BUSY;
        size_t limit = H5_FANOUT_SEND_LIMIT();
        size_t used = (TiRtcGetSendBufferUsed)(peers[i]);
        size_t budget = video ? limit - limit / 8U : limit;
        if (!limit || (used < budget && frame->length <= budget - used))
            sent = video ? (TiRtcSendVideoStream)(peers[i],frame,data) :
                           (TiRtcSendAudioStream)(peers[i],frame,data);
        if (sent >= 0) result = sent;
        token = h5_fanout_lock();
        h5_fanout_peer_t *p = h5_fanout_find(peers[i]);
        if (p && video && frame->media == TIRTC_VIDEO_H264) {
            if (sent < 0) { request_key |= !p->needs_key; p->needs_key = true; }
            else if (frame->flags & TIRTC_FRAME_FLAG_KEY_FRAME) p->needs_key = false;
        }
        h5_fanout_unlock(token);
        h5_fanout_release(peers[i]);
    }
    if (request_key && H5_FANOUT_ACTIVE()) {
        uint32_t now = H5_FANOUT_NOW_MS();
        token = h5_fanout_lock();
        bool due = !s_h5_key_requested || (uint32_t)(now - s_h5_key_requested_ms) >= 1000U;
        if (due) { s_h5_key_requested = true; s_h5_key_requested_ms = now; }
        h5_fanout_unlock(token);
        if (due) on_request_key_frame((tirtc_conn_t)atomic_load(&s_connection), H5_FANOUT_VIDEO_ID);
    }
    return result;
}
static int h5_fanout_send_audio(tirtc_conn_t c,const TIRTCFRAMEINFO*f,const void*d)
{
    return h5_fanout_send(c,f,d,false);
}
static int h5_fanout_send_video(tirtc_conn_t c,const TIRTCFRAMEINFO*f,const void*d)
{
    return h5_fanout_send(c,f,d,true);
}
static size_t h5_fanout_buffer_used(tirtc_conn_t connection)
{
    tirtc_conn_t peers[XIAOTAI_H5_VIEWERS];
    unsigned count = 0;
    unsigned token = h5_fanout_lock();
    bool grouped = H5_FANOUT_ACTIVE();
    if (grouped) for (unsigned i=0;i<XIAOTAI_H5_VIEWERS;++i) {
        h5_fanout_peer_t *p=&s_h5_peers[i];
        if(p->connection && !p->closing && p->video) {++p->references;peers[count++]=p->connection;}
    }
    h5_fanout_unlock(token);
    if(!grouped) return (TiRtcGetSendBufferUsed)(connection);
    /* Producer can progress if at least one viewer can accept a frame. Each
     * actual send still enforces that individual viewer's bounded budget. */
    size_t used=0;
    for(unsigned i=0;i<count;++i) {
        size_t value=(TiRtcGetSendBufferUsed)(peers[i]);
        if(i==0 || value<used)used=value;
        h5_fanout_release(peers[i]);
    }
    return used;
}

static int h5_fanout_subscribe_audio(tirtc_conn_t connection, uint8_t stream_id)
{
    unsigned token = h5_fanout_lock();
    bool grouped = H5_FANOUT_ACTIVE();
    h5_fanout_peer_t *p = grouped ? h5_fanout_find((tirtc_conn_t)atomic_load(&s_connection)) : NULL;
    tirtc_conn_t owner = p && !p->closing ? p->connection : NULL;
    if (owner) ++p->references;
    h5_fanout_unlock(token);
    if (!grouped) return (TiRtcSubscribeAudio)(connection, stream_id);
    if (!owner) return TIRTC_E_BUSY;
    int result = (TiRtcSubscribeAudio)(owner, stream_id);
    h5_fanout_release(owner);
    return result;
}
