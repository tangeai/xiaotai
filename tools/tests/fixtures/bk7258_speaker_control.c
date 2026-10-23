static int onboard_speaker_play_control(audio_play_t *play, audio_play_ctl_t ctl)
{
    int ret = BK_OK;

    ONBOARD_SPK_PLAY_CHECK_NULL(play);

    onboard_speaker_play_priv_t *priv = (onboard_speaker_play_priv_t *)play->play_ctx;
    ONBOARD_SPK_PLAY_CHECK_NULL(priv);

    if (ctl == AUDIO_PLAY_PAUSE)
    {
        if (onboard_speaker_state_get(priv) == AUDIO_PLAY_STA_PAUSED)
        {
            return BK_OK;
        }

        /* first mute audio dac, then pause audio pipeline */
        ret = onboard_speaker_stop(priv);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard spk paly pause fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
        onboard_speaker_state_set(priv, AUDIO_PLAY_STA_PAUSED);
    }
    else if (ctl == AUDIO_PLAY_RESUME)
    {
        if (onboard_speaker_state_get(priv) == AUDIO_PLAY_STA_RUNNING)
        {
            return BK_OK;
        }

        /* first resume audio play, then unmute audio dac */
        ret = onboard_speaker_start(priv);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard spk paly start fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
        onboard_speaker_state_set(priv, AUDIO_PLAY_STA_RUNNING);
    }
    else if (ctl == AUDIO_PLAY_MUTE)
    {
        ret = onboard_speaker_mute();
        if (ret != BK_OK)
        {
            LOGE("%s, onboard spk paly mute fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
    }
    else if (ctl == AUDIO_PLAY_UNMUTE)
    {
        ret = onboard_speaker_unmute();
        if (ret != BK_OK)
        {
            LOGE("%s, onboard spk paly unmute fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
    }
    else if (ctl == AUDIO_PLAY_SET_VOLUME)
    {
        ret = onboard_speaker_set_volume(play->config.volume);
        if (ret != BK_OK)
        {
            LOGE("%s, onboard spk paly set volume fail, ret: %d, %d \n", __func__, ret, __LINE__);
        }
    }
    else
    {
        //nothing to do
    }

    return ret;
}
