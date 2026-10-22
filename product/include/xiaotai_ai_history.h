#ifndef XIAOTAI_AI_HISTORY_H
#define XIAOTAI_AI_HISTORY_H

#include <string.h>
#include "xiaotai_ai_view.h"

#define XIAOTAI_AI_HISTORY_CAPACITY 12U

typedef struct {
    int speaker;
    char text[256];
} xiaotai_ai_history_entry_t;

typedef struct {
    xiaotai_ai_history_entry_t entries[XIAOTAI_AI_HISTORY_CAPACITY];
    unsigned count;
    uint32_t generation;
    int speaker;
    char utterance_id[65];
    bool current;
} xiaotai_ai_history_t;

/* The runtime owns this bounded history. Partial captions replace the current
 * utterance; round boundaries and session generations never merge speakers. */
static inline void xiaotai_ai_history_update(xiaotai_ai_history_t *history,
                                            const xiaotai_ai_view_t *view,
                                            uint32_t generation)
{
    if (history == NULL || view == NULL) return;
    if (view->caption_type < 0) {
        history->current = false;
        return;
    }
    if (view->caption[0] == '\0') return;
    bool same = history->current && history->count != 0U &&
                history->generation == generation &&
                history->speaker == view->caption_type &&
                strcmp(history->utterance_id, view->utterance_id) == 0;
    if (!same) {
        if (history->count == XIAOTAI_AI_HISTORY_CAPACITY) {
            memmove(history->entries, history->entries + 1,
                    (XIAOTAI_AI_HISTORY_CAPACITY - 1U) * sizeof(history->entries[0]));
            --history->count;
        }
        ++history->count;
    }
    xiaotai_ai_history_entry_t *entry = &history->entries[history->count - 1U];
    entry->speaker = view->caption_type;
    memcpy(entry->text, view->caption, sizeof(entry->text));
    entry->text[sizeof(entry->text) - 1U] = '\0';
    memcpy(history->utterance_id, view->utterance_id, sizeof(history->utterance_id));
    history->utterance_id[sizeof(history->utterance_id) - 1U] = '\0';
    history->generation = generation;
    history->speaker = view->caption_type;
    history->current = true;
}

#endif
