# ATK-DNESP32S3 小钛固件工程

这是正点原子 ATK-DNESP32S3 开发板的 ESP-IDF（乐鑫物联网开发框架）工程。
面向使用者的规格、编译、烧录和体验步骤见
[开发板使用指南](../../../../docs/boards/alientek-atk-dnesp32s3/README.md)。

## 构建

建议从仓库根目录使用统一入口：

```bash
python3 tools/build.py --board alientek-atk-dnesp32s3
```

需要直接操作工程时，先准备 ESP-IDF 5.5.x，再执行：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <串口> flash monitor
```

默认配置面向 16 MB Flash（闪存）和 8 MB PSRAM（伪静态随机存取存储器）。
修改模组后，必须同步检查 `sdkconfig.defaults`、分区表和板级配置。

## 代码边界

- `main/`：工程入口和模块组装；
- `components/starter_runtime/`：会话运行时；
- `components/starter_media/`：媒体任务和板级媒体接入；
- `components/starter_tirtc/`：TiRTC（实时音视频软件开发工具包）适配；
- `components/platform_client/`：服务发现、HTTP（超文本传输协议）和消息连接；
- `components/wifi_manager/`：无线网络和设备配网；
- `components/runtime_config/`：设备配置存储；
- `third_party/tirtc/`：当前工程使用的 TiRTC 文件。

板级引脚和器件初始化位于 `boards/alientek/atk-dnesp32s3/`。产品业务规则不能
放回板级驱动，也不能让其他开发板直接引用本工程作为公共代码目录。

## 验证

提交改动前，先运行仓库检查，再构建完整固件：

```bash
python3 tools/build.py --validate
python3 -m unittest discover -s tools/tests -v
python3 tools/build.py --board alientek-atk-dnesp32s3
```

完整构建只证明可以生成固件。显示、摄像头、音频和产品功能仍需在准确型号的实板上验证。
