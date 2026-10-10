# P4 OV5647 原生 VGA 采集实验（build.72）

build.71 的双端日志显示：关闭本机摄像头后，下行转换平均耗时从约 141ms 降至
51～74ms，重新开启后回升。该对照支持并发资源竞争，尚不能单独区分 CPU、PPA、
内存带宽和无线负载，也不能证明下述修改已经消除声音或视频卡顿。

## 参考与适配边界

- 乐鑫 `esp-video-components` 的 `video_custom_format` 示例通过
  `VIDIOC_S_SENSOR_FMT` 配置自定义传感器格式，再协商采集像素格式。
  本项目已有对应接口，OV5647 `set_format` 接受外部格式表，保持指针引用。
  参考：<https://github.com/espressif/esp-video-components/tree/master/esp_video/examples/video_custom_format>。
- 截至 2026-10-11 检索到的乐鑫 OV5647 驱动未提供可直接复用的 VGA 模式；
  上游还有缺少 VGA 模式的议题：
  <https://github.com/espressif/esp-video-components/issues/30>。
- Linux OV5647 驱动存在全视野 VGA 的传感器合并像素和抽样模式：
  <https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ov5647.c>。
  本实现参考其 VGA 抽样寄存器含义，不复制 Linux 驱动实现或直接照搬其时钟配置。

新增平台内 `ov5647_vga_capture.h`：基于当前锁定的乐鑫 24MHz RAW10
1280×960/45fps 模式复制寄存器表，输出尺寸改为 640×480，抽样步进改为
0x35，补充 VGA 采样控制。保留当前 PLL、MIPI 时序、方向与传感器窗口。
自定义 ISP 的 HTS/VTS 分别为 1796/1093，与寄存器 0x380c-f 对齐；原模式元数据
两者颠倒，本轮只修正新模式的副本。全部表使用静态存储，满足驱动引用生命周期。
这是一项待实板验证的组合，不等同于乐鑫已经验证过的 VGA 模式。

## 行为变化

- 设备视频对讲：640×480、5fps、600kbps；微信视频：640×480、8fps、500kbps。
  应用发送参数不变；采集目标改为原生 VGA，通过已有 `yuv420-direct` 编码路径。
- 传感器时钟和标称 45fps 保留，应用按 5/8fps 取帧；没有宣称传感器本身降至
  5/8fps。本轮旨在缩小 CSI/ISP 帧尺寸并绕过上行 PPA 缩放。
- H5 仍为 1280×960、20fps、3Mbps，采用原模式。非 VGA 采集走已有格式协商。
- 不旋转上行像素；接收显示方向、音频、队列容量、优先级与拥塞策略不变。
- VGA 格式协商失败时返回错误，不冒充原生采集成功；无需缩放器时预热直接成功，
  避免阻断视频初始化。没有修改或复制整个 managed component。

## 验证与烧录验收

主机测试 `tools/tests/test_p4_native_vga.py` 编译生产格式协商代码，覆盖原生尺寸、
寄存器副本、持久引用、重启、非 VGA 场景、未知源模式和 ioctl 失败。
`test_full_frame_uplink.py` 经公开策略入口验证设备/微信原生采集及 H5 原参数。
测试按修改前失败、修改后通过执行，并注册 `tools/check.sh`。

本机没有串口设备；本轮没有烧录，也没有证明实板出图或性能改善。两台 P4 烧录
相同版本后依次验证：

1. 相机首帧应为 `source=640x480 input=yuv420-direct output=640x480`，
   编码前 `convert` 应接近零；实际 YUV420 帧为 460800 字节。
2. 比较原版的画面方向、颜色、视野、边缘、曝光及亮暗环境；抽样可能改变细节和
   抗混叠表现，不能仅以尺寸正确验收。
3. 两端移动画面并持续说话，记录每 30 秒 `status` 的音频溢出增量、转换耗时、
   视频接收/显示帧率及发送缓存；与 build.71 相同条件对比。
4. 重复摄像头开关、挂断重拨、设备/微信/H5 切换，确认 H5 恢复 1280×960，
   对讲恢复 VGA。持续通话和 AEC 双讲另行验收。

build.71 回退基线提交：`96a6dd3`。仅在上述实板验证完成后才能认定本实验有效。
