// Copyright 2024-2025 Beken
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


/* Beken release/v3.1.1.10; Apache-2.0. Control function fixture. */
static int onboard_mic_record_control(audio_record_t *record, audio_record_ctl_t ctl)
{
    int ret = BK_OK;

    onboard_mic_record_priv_t *priv = (onboard_mic_record_priv_t *)record->record_ctx;
    ONBOARD_MIC_RECORD_CHECK_NULL(priv);

    if (ctl == AUDIO_RECORD_PAUSE)
    {
        if (onboard_mic_state_get(priv) == AUDIO_RECORD_STA_PAUSED)
        {
            return BK_OK;
        }

        ret = onboard_mic_stop(priv);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard mic record pause fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
        onboard_mic_state_set(priv, AUDIO_RECORD_STA_PAUSED);
    }
    else if (ctl == AUDIO_RECORD_RESUME)
    {
        if (onboard_mic_state_get(priv) == AUDIO_RECORD_STA_RUNNING)
        {
            return BK_OK;
        }

        ret = onboard_mic_start(priv);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard mic record start fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
        onboard_mic_state_set(priv, AUDIO_RECORD_STA_RUNNING);
    }
    else if (ctl == AUDIO_RECORD_SET_ADC_GAIN)
    {
        ret = onboard_mic_set_adc_gain(record->config.adc_gain);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard mic set adc gain fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
    }
    else
    {
        //nothing to do
    }

    return ret;
}
