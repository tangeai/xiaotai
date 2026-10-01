# 共享源码编译合同

本目录不参与固件的常规顶层构建。`CMakeLists.txt` 仅作为迁移期间的编译合同，供主机测试确认
ESP32-P4 工程从 `platforms/esp-idf/` 取得共享产品源码，而不是重新复制板卡私有实现。

实际固件入口见 `../main/CMakeLists.txt` 和 `../components/`。正常编译请使用仓库根目录的
`tools/build.py`。
