# Screen Debug Server

This is a legacy module, not an entry point in the current shared XiaoTai
startup. Its switch alone does not enable it in the current firmware. Check
the selected component's CMake source registration before using these routes.

This module is a Wi-Fi LAN debug helper for viewing and driving the device screen from a browser.

Build switch:

`CONFIG_APP_DEBUG_SCREEN_SERVER_ENABLE`

When disabled, this folder is not compiled and no debug server task is started.

When enabled, browse to:

`http://<device-ip>:8080/`

The browser view follows the active LVGL viewport. The former 480 x 320 layout
does not describe the current 800 x 480 product UI.

Routes:

- `/` browser debug page
- `/screen.bmp` current LVGL screen snapshot
- `/api/status` JSON status
- `/api/tap?x=<x>&y=<y>` dispatch a screen tap
- `/api/scroll?x=<x>&y=<y>&dx=<dx>&dy=<dy>` dispatch a screen scroll
