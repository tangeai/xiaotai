#include "starter_media.h"
#include <stdatomic.h>
static _Atomic(starter_pcm_observer_t) s_observer;
void starter_media_set_pcm_observer(starter_pcm_observer_t observer)
{ atomic_store(&s_observer, observer); }
void starter_media_observe_pcm(const int16_t *pcm, size_t samples, bool sound,
                               bool safe, int64_t captured_ms)
{
    starter_pcm_observer_t observer=atomic_load(&s_observer);
    if(observer) observer(pcm,samples,sound,safe,captured_ms);
}
