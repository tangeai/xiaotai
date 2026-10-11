#!/usr/bin/env python3
"""Configure the actual P4 SDK component and validate the linked archive."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
project = root / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c"
sdk = project / "third_party/tirtc"
component = project / "components/tirtc_sdk"
with tempfile.TemporaryDirectory(prefix="p4-sdk-link-") as directory:
    p = Path(directory)
    (p / "dummy.c").write_text("int sdk_link_test;\n")
    (p / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
project(p4_sdk_link C)
set(IDF_TARGET esp32p4)
set(IDF_VERSION_MAJOR 5)
set(IDF_VERSION_MINOR 5)
set(IDF_VERSION_PATCH 4)
set(CONFIG_FREERTOS_HZ 1000)
function(idf_component_register)
  add_library(sdk_component STATIC "${{CMAKE_SOURCE_DIR}}/dummy.c")
  set(COMPONENT_LIB sdk_component PARENT_SCOPE)
endfunction()
foreach(name __idf_lwip __idf_mbedtls mbedcrypto mbedtls mbedx509 everest p256m __idf_hal __idf_soc)
  add_library(${{name}} STATIC "${{CMAKE_SOURCE_DIR}}/dummy.c")
endforeach()
include("{component}/CMakeLists.txt")
get_target_property(archive tirtc_single_prebuilt IMPORTED_LOCATION)
file(WRITE "${{CMAKE_BINARY_DIR}}/sdk-archive.txt" "${{archive}}")
''')
    result = subprocess.run(["cmake", "-S", str(p), "-B", str(p / "build")], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    archive = Path((p / "build/sdk-archive.txt").read_text())
    assert archive.resolve() == (sdk / "lib/libTiRTC.a").resolve()
    data = archive.read_bytes()
    assert b"v2.5.0\x00" in data and b'"tirtc":"v2.5.0-' in data
    with (p / "member.o").open("wb") as output:
        subprocess.run(["ar", "p", str(archive), "crc32.o"], stdout=output, check=True)
    assert "RISC-V" in subprocess.check_output(["readelf", "-h", str(p / "member.o")], text=True)
print("PASS: actual P4 component selects the supplied RISC-V TiRTC 2.5.0 archive")
