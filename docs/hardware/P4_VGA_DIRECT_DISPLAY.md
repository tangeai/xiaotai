# P4 设备视频对讲 VGA 下行直转实验（build.73）

## 参数与范围

设备对讲上下行固定 640×480；设备发送 5fps/600kbps、GOP 10，原生 VGA
采集及直接硬件编码保持 build.72。微信、H5、音频、任务配置和拥塞策略不变。
本轮只优化设备对讲 H264 下行原尺寸、零旋转、零裁剪转换，不改变解码器。

## 官方参考和实现

乐鑫 `esp_image_effects` 提供 P4 优化的 I420 → RGB565_LE/BE 直转接口：

- [官方接口](https://github.com/espressif/esp-adf-libs/blob/89f77720037ffa227e7bbbf8854b039109d3326c/esp_image_effects/include/esp_imgfx_color_convert.h)
- [官方性能参考](https://github.com/espressif/esp-adf-libs/blob/89f77720037ffa227e7bbbf8854b039109d3326c/esp_image_effects/doc/PERFORMANCE_ESP32P4.md)

固定 registry 版本 1.1.0 和锁文件哈希，使用其原有 ESP32-P4 库，避免随新版
组件切换硅片库。显式固定 esp-nn 1.3.2，防止解析新依赖时顺带升级唤醒推理库。

原链路：I420 → CPU pack 为 PPA YUV420 → PPA RGB565 → CPU swap。
新链路：I420 → 官方优化转换直接输出所需字节序 RGB565。
采用 BT.601；PPA staging 保留给旋转、裁剪、其他尺寸和失败回退，不宣称节省该池。
转换句柄每个 converter 独立，单转换 worker 串行调用，销毁时关闭。保留原来的
禁止标量软件回退限制；库打开/转换失败仍尝试 PPA。真实输出颜色需对照验收。

## 检查与验收

主机回归覆盖公开转换入口的路径选择、字节序配置、句柄复用/释放、库失败和
旋转/其他尺寸回退。它验证 SDK 调用契约，不运行预编译 P4 指令，也不证明提速。
仓库完整检查和 ESP-IDF 构建单独留存；没有自动烧录或实板验收。

两台设备都烧录 build.73 后，在与 build.72 相同网络/场景/运动条件下检查：

1. `status` 固件身份一致；相机仍 `source=640x480 input=yuv420-direct`。
2. 原角度下应出现 `I420 direct display: engine=esp_image_effects`，周期 VRX
   为 `engine=imgfx-direct`。`pack/PPA/swap` 对该路径为零，真实转换时间看
   `ms=au/cvt/ppa` 的 `cvt`，不能把 PPA 为零当作转换零耗时。
3. 对照红绿蓝、黑白、肤色和灰阶，检查 RGB 顺序、字节序、亮度、视野和边缘。
   双讲期间记录两端音频 overflow 增量、下行 decode/convert/present、输入 gap
   和发送缓存。至少连续通话 10 分钟并移动两端画面。
4. 转向 90/180/270°应回到 `engine=ppa`；转回零角度恢复直转。重复挂断、重拨、
   微信/H5 切换，检查资源释放和原功能。

原生 VGA 去掉上行缩放后仍有传输积压；本轮不宣称修复 SDK/无线传输瓶颈。
基线：build.72，提交 2743679，ELF 3f5fe0047。
