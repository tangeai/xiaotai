#!/usr/bin/env python3
"""Configure the production S3 SDK component and validate the linked archives."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
for name in ("lckfb-esp32s3-xiaotai",):
    project = root / "firmware/esp-idf/esp32s3" / name
    sdk = root / "platforms/esp-idf/third_party/tirtc/esp32s3"
    component = root / "platforms/esp-idf/components/tirtc_sdk"
    with tempfile.TemporaryDirectory(prefix="s3-sdk-link-") as directory:
        p = Path(directory)
        (p / "dummy.c").write_text("int sdk_link_test;\n")
        (p / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
    project(s3_sdk_link C)
    set(IDF_TARGET esp32s3)
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
    get_target_property(archive tirtc_esp32s3_prebuilt IMPORTED_LOCATION)
    file(WRITE "${{CMAKE_BINARY_DIR}}/sdk-archive.txt" "${{archive}}")
    ''')
        result = subprocess.run(["cmake", "-S", str(p), "-B", str(p / "build")], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        archive = Path((p / "build/sdk-archive.txt").read_text())
        assert archive.resolve() == (sdk / "lib/libTiRTC.a").resolve()
        data = archive.read_bytes()
        assert hashlib.sha256(data).hexdigest() == "87f9fc6eac5d1c5bd1bd4877fc00abdd115101e85181c035e76089343486f850"
        assert b"v2.6.3\x00" in data and b'"tirtc":"v2.6.3-' in data
        header = (sdk / "include/tirtc/tiRTC.h").read_text()
        assert "#define TIRTC_VERSION_PATCH     3" in header
        assert "TiRtcSetAudioRedundancyLossThreshold" in header
        symbols = subprocess.check_output(["nm", "--defined-only", str(archive)], text=True)
        assert " T TiRtcSetAudioRedundancyLossThreshold" in symbols
        for line in (sdk / "SHA256SUMS.txt").read_text().splitlines():
            digest, relative = line.split(maxsplit=1)
            assert hashlib.sha256((sdk / relative).read_bytes()).hexdigest() == digest, relative
        with (p / "member.o").open("wb") as output:
            subprocess.run(["ar", "p", str(archive), "crc32.o"], stdout=output, check=True)
        assert "Xtensa" in subprocess.check_output(["readelf", "-h", str(p / "member.o")], text=True)
    print("PASS: actual S3 component selects the supplied Xtensa TiRTC 2.6.3 archive")

for project_name, hardware_ir in (
    ("lckfb-esp32s3-xiaotai", project / "hardware-ir.json"),
    ("m5stack-cores3-lite", root / "boards/m5stack/cores3-lite/hardware-ir.json"),
):
    metadata = json.loads(hardware_ir.read_text())["toolchain"]["tirtc"]
    assert metadata["version"] == "2.6.3", hardware_ir
    assert metadata["archive_sha256"] == hashlib.sha256(data).hexdigest(), hardware_ir
    guide = root / "firmware/esp-idf/esp32s3" / project_name / "README.md"
    assert "TiRTC SDK 2.6.3" in guide.read_text(), guide
