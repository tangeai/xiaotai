# AI intercom on BK7258: concealing short gaps in Opus downlink audio

[中文](bk-opus-plc.md) · [English](bk-opus-plc.en.md)

When a piece of an AI reply goes missing, locate the gap, compare playback of the same speech on a computer, then listen on the board. This example uses TiRTC downlink audio on BK7258 with 16 kHz mono Opus and 20 ms per packet. Run the commands from the root of a checkout containing this implementation.

## 1. Locate the gap and the decoder

Follow `on_audio` in the [TiRTC receive callback](../../platforms/beken/runtime/src/xiaotai_tirtc.c) to `playback_task` in the [audio runtime](../../platforms/beken/runtime/src/xiaotai_audio.c). Compare media timestamps, arrival times and queue overflow counts for one stream. If two packets arrive 60 ms apart but their media timestamps advance by only 20 ms, inserting another frame would stretch the speech.

The playback worker calls the SDK's libopus software decoder on the AP core. Its public API supports packet loss concealment (PLC). In-band forward error correction (FEC) lets the sender include redundancy for the preceding frame in the next packet. Check what the sender produces and what the decoder accepts. [Opus decoder API](https://opus-codec.org/docs/opus_api-1.3.1/group__opus__decoder.html) · [RFC 7587 §3.3](https://www.rfc-editor.org/rfc/rfc7587.html#section-3.3)

## 2. Compare playback on a computer

On Ubuntu, install the dependencies and run the replay against the same C module as the firmware. On other hosts, set `OPUS_HEADERS` to the directory containing `opus.h` and `OPUS_LIBRARY` to the host libopus library file.

```bash
sudo apt-get install build-essential libopus-dev python3
python3 tools/tests/test_bk_opus_plc.py
```

The [test driver](../../tools/tests/test_bk_opus_plc.py) resamples the included female and male voice prompts to 16 kHz. The [replay](../../tests/audio/bk_opus_plc_replay.c) encodes them and drops whole packets with a fixed seed. Three independent decoders produce a lossless-delivery reference, a baseline that skips missing packets, and audio with concealment. The replay aligns them on the media timeline and compares root mean square amplitude in 5 ms windows. Read `dropped` for lost packets, `filled` for concealed frames, and `MAE` for mean absolute envelope error. Lower error is closer to the reference.

For example, random loss removed 18 of 60 packets from the female prompt. The module filled 10 frames and reduced error from 662.6 to 471.3. Inspect all four combinations of voice and loss pattern (random or clustered at 30%). Check sample equality before loss and confirm that late packets for concealed frames do not play twice. A successful run ends with `OK`. Values can vary with libopus version. Measuring sentence intelligibility requires more than envelope error on two short prompts.

## 3. Integrate with the playback worker and bound the cost

1. Enqueue the stream ID, round, media timestamp, receive time and sample count with each packet. Call the [concealment module](../../platforms/beken/runtime/src/xiaotai_opus_plc.c) after dequeue and before speaker output. One playback worker owns the decoder and concealment state.
2. Observe two consecutive 20 ms media intervals in the same stream and round. A subsequent 40 ms timestamp step may indicate one missing frame, and 60 ms may indicate two. An arrival gap over 120 ms, stream or round change, or short discontinuous transmission packet breaks the cadence. Wait for the next packet when the queue is empty.
3. For one missing frame, pass the next packet to `opus_decode` with `frame_size=320` and `decode_fec=1`. Libopus applies PLC if no redundancy is available. For two missing frames, first pass a NULL data pointer and zero bytes to conceal the earliest 320 samples, then recover the most recent missing frame. Finally decode the same real packet with `decode_fec=0`. Output the samples in media order.
4. Each frame contains 320 samples at 16 kHz. Two concealed frames and the current frame fit the existing 960-sample output buffer. Decode ordinary packets once. Reuse the speaker and echo reference paths to avoid another worker or per-packet allocation.

The 30% setting is expected loss on the **device's Opus encoder**. This example uses 20 kbit/s, complexity 3 and in-band FEC. The default AI uplink uses A-law. To provide redundancy on the AI downlink, configure the server's Opus encoder. The receiver requests recovery for observed gaps. Libopus selects its internal algorithm by coding mode and needs no direct tuning of `silk_PLC`. [Opus encoder controls](https://opus-codec.org/docs/opus_api-1.3.1/group__opus__encoderctls.html)

## 4. Verify on the board before porting

Follow the [MGKFB dual-screen guide](../boards/mgkfb-bk7258/README.en.md) to prepare the toolchain and flash the resulting image. Build from the repository root. For LCKFB, use its own board ID and image. [LCKFB board guide](../boards/lckfb-bk7258/README.en.md)

```bash
python3 tools/build.py --board mgkfb-bk7258
```

Play the same AI reply with and without controlled packet loss. Record the sender's drop positions, the playback log's `plc-ms`, queue overflows, decode time and speaker output. Drop one or two adjacent frames during steady speech. If the next packet qualifies for recovery and the speaker write succeeds, expect `plc-ms` to increase by 20 or 40 respectively. The counter includes both FEC and PLC. Listen for gaps and repeated audio across turns. Measure peak CPU use and playback stack headroom against a baseline with concealment disabled under the same conditions.

Host replay and both BK7258 builds passed, and the MGKFB dual-screen board was flashed and booted. Listening under real network loss and peak resource measurements remain open. On a new device, check whether the SDK already conceals loss, establish the timestamp unit, packet duration and output capacity, then repeat the replay at the same loss positions. This TiRTC callback lacks a sender sequence number, so a sender pause of exactly one or two frames can look like loss. The final packet has no successor to confirm a gap, and longer bursts exceed the recovery budget.

## Reproduce the workflow with AI assistance

Give the assistant the board, SDK version, audio format and test entry point. Use the output of each step to decide what to ask next.

- **Trace playback.** “Trace the receive callback to the speaker. Identify source locations, timestamp units, decoder ownership and existing concealment. List any evidence still missing.”
- **Compare decoders.** “Compare reference, skip and concealment paths on the same encoded packets. Report loss positions, duration filled and error. Explain one gap that could not be filled.”
- **Verify on hardware.** “Recalculate the maximum output sample count at the board's sample rate and identify the integration point. Use this serial log and recording to distinguish observed behavior from measurements still needed.”
