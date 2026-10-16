# 立创·实战派 ESP32-S3 v1.0.0

发布日期：2026-10-16。GitHub 标签：`esp32s3-v1.0.0`。

适用板卡：`lckfb-esp32s3`，PCB V1.0.1，ESP32-S3-WROOM-1-N16R8，
16 MB Flash / 8 MB PSRAM。其他 ESP32-S3 开发板不能直接使用此包。

## 新增发布内容

这是本板首个独立的 v1.0.0 GitHub Release，将当前工程已有功能打包提供下载：

- 320×240 触控界面、Wi-Fi 配网、设备绑定、联系人和设备设置。
- H5 网页实时查看与双向对讲，摄像头输出 MJPEG 320×240、目标 8 fps。
- AI 语音对话，双向 Opus 16 kHz；设备呼叫和微信网络语音通话使用 A-law 8 kHz。
- ES7210 采集、ES8311 播放，以及基于扬声器回采参考的 16 kHz AEC。
- 离线唤醒、按键控制和多人房间对讲。
- 完整烧录文件、地址配置、应用 ELF、源码提交号及 SHA-256 清单。

这些功能属于当前固件的发布范围，不表示本次文档提交新增了对应实现。

## 本次修改

- 首页“选择开发板和固件”主表同时展示 BK7258、立创 ESP32-S3 和微雪 ESP32-P4，
  提供各自 v1.0.0 固件的直接下载入口和开发板图片。
- S3 开发板指南和开发板目录增加本板 Release 链接。
- 修正 S3 指南中过时的 AI 音频说明：AI 使用 Opus 16 kHz，H5 和语音呼叫使用
  A-law 8 kHz；工程简介同步当前 TiRTC 2.5.0 依赖版本。
- 明确 ESP32 必须按清单烧录完整文件集合，BK7258 的完整镜像与 OTA 镜像用途不同。

## 构建与验证

- 固件应用版本：`1.0.0`；原生工程目录：
  `firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/`，产物位于其 `build/`。
- ESP-IDF 5.5.5，TiRTC 2.5.0；使用本板既有构建配置和依赖锁定文件。
- 发布前执行 `bash tools/check.sh`、原生固件构建、BIN/ELF 身份核对及包内文件校验。
- 板卡已有实板跑通记录；本次新生成的精确 BIN 尚未连接实板复验，包内状态保留为
  `BUILD_VERIFIED_HIL_PENDING`。待验证项包括首次烧录、配网绑定、H5 音视频、AI 多轮
  对话、设备/微信语音呼叫、房间对讲和长时间稳定性。

## 下载和烧录

下载 `lckfb-esp32s3-1.0.0-validation.zip`，用同一 Release 的 `SHA256SUMS`
核对压缩包，再解压查看 `MANIFEST.json` 和 `README.txt`。按 `flash_files`
写入全部文件，不要只烧录应用 BIN。包中固件是原生 `build/` 产物的副本。

应用文件、完整 SHA-256 和源码提交号以包内清单及 GitHub Release 说明为准。
具体操作见[开发板指南](README.md#烧录)。
