"""All five boards must report the version from the linked SDK."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
p4 = root / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main"
display = (p4 / "ui/display.c").read_text()
adapters = [
    (root / "platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c", "starter_tirtc_version"),
    (root / "platforms/beken/runtime/src/xiaotai_tirtc.c", "xiaotai_tirtc_version"),
    (p4 / "ui/display.c", "display_tirtc_version"),
]
# Compile the actual getters against a changing SDK response to catch stale literals.
for source, name in adapters:
    getter = re.search(r"const char \*" + name + r"\(void\)\s*\{[^}]*\}", source.read_text())
    assert getter, f"{source} lacks the runtime getter {name}"
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "version"
        program = """
#include <string.h>
static const char *sdk;
static const char *TiRtcGetVersion(void) { return sdk; }
""" + getter.group() + f"""
int main(void) {{
    sdk = "v1.2.3";
    if (strcmp({name}(), sdk)) return 1;
    sdk = "v7.8.9";
    return strcmp({name}(), sdk);
}}
"""
        subprocess.run(["cc", "-x", "c", "-", "-o", binary], input=program, text=True, check=True)
        subprocess.run([binary], check=True)

assert re.search(r'display_create_info_row\(s_ota_page,\s*[^,]+,\s*"TiRTC",\s*display_tirtc_version\(\)', display)
assert re.search(r'display_text_set\(s_ota_second_value_label,\s*[^;]+target_version : display_tirtc_version\(\)', display)

# The default P4 build shares the S3 startup path. Both BK boards share xiaotai_app.
esp_startup = (root / "platforms/esp-idf/main/app_main.c").read_text()
bk_startup = (root / "platforms/beken/runtime/src/xiaotai_app.c").read_text()
assert re.search(r'ESP_LOGI\([^;]*starter_tirtc_version\(\)', esp_startup)
assert re.search(r'BK_LOGI\([^;]*xiaotai_tirtc_version\(\)', bk_startup)
