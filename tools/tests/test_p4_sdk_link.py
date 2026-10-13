#!/usr/bin/env python3
"""Configure the actual P4 SDK component and validate the linked archive."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
project = root / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c"
sdk = project / "third_party/tirtc"
component = project / "components/tirtc_sdk"
expected = "6c5c1543f4a099c2f8fdfdff5b50ef728cf181ce09931c2798d34e84d22473b1"
assert hashlib.sha256((sdk / "lib/libTiRTC.a").read_bytes()).hexdigest() == expected, "P4 SDK candidate not installed"
assert "LIB_SHA256=" + expected in (sdk / "manifest/build-contract.env").read_text()

with tempfile.TemporaryDirectory(prefix="p4-sdk-link-") as directory:
    p = Path(directory)
    (p / "dummy.c").write_text("int sdk_link_test;\n")
    (p / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
project(p4_sdk_link C)
set(IDF_TARGET esp32p4)
set(IDF_VERSION_MAJOR 5)
set(IDF_VERSION_MINOR 5)
set(IDF_VERSION_PATCH 5)
set(CONFIG_FREERTOS_HZ 1000)
set(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE ON)
set(CONFIG_LWIP_MAX_SOCKETS 16)
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
    # The application baseline is deliberately upgraded; incompatible IDF
    # versions remain rejected rather than bypassing the component gate.
    cmake = p / "CMakeLists.txt"
    cmake.write_text(cmake.read_text().replace("set(IDF_VERSION_PATCH 5)",
                                             "set(IDF_VERSION_PATCH 4)"))
    result = subprocess.run(["cmake", "-S", str(p), "-B", str(p / "old-idf")],
                            capture_output=True, text=True)
    assert result.returncode != 0 and "requires ESP-IDF 5.5.5" in result.stderr
print("PASS: actual P4 component selects the supplied RISC-V TiRTC 2.5.0 archive")
