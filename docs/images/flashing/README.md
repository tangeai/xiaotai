# 烧录操作图

网页来源：[乐鑫 ESP Web Tool](https://espressif.github.io/esptool-js/)。截图日期：2026-10-22。
红框和中文提示在浏览器页面上添加后截图；网页版本变化后，应重新核对按钮和字段名称。

- `web-connect.png`：网页初始连接区域，标出 Program 的波特率和 Connect。
- `web-<board-id>.png`：各板完整镜像的参数示意。为展示连接后使用的字段，在未连接设备的页面中展开参数区，填写 `0x0`、选择发布镜像，并把三项 Flash 参数设为 `keep`；没有执行连接或烧录。
- `web-split-<board-id>.png`：分文件参数示意。使用同版 ZIP 内的实际 BIN，按 `MANIFEST.json` 添加 3 行（S3/M5）或 4 行（P4），标红各地址、文件和操作按钮；没有执行烧录。
- `project-<board-id>.png`：从指南命令排版生成的命令示意图，标出本板 ID、工程目录和串口占位符，不是实际执行记录。

图片不作为实板烧录或启动验证证据。操作步骤和可复制的命令以各开发板指南为准。
