"""Compile the production JPEG encoder and verify the board's build policy."""
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai'
CAMERA = PROJECT / 'managed_components/espressif__esp32-camera'

class S3JpegPerformance(unittest.TestCase):
    def exercise_build(self, real_encoder):
        # Run the actual board CMake hook with a minimal IDF component facade;
        # no ESP-IDF installation or board is required by the repository gate.
        project_cmake = (PROJECT / 'CMakeLists.txt').read_text()
        hook = re.search(r'# BEGIN JPEG PERFORMANCE\n(.*?)# END JPEG PERFORMANCE',
                         project_cmake, re.S)
        hook = hook[1].replace('${CMAKE_CURRENT_LIST_DIR}', str(PROJECT)) if hook else ''
        with tempfile.TemporaryDirectory(prefix='s3-jpeg-') as tmp:
            base = Path(tmp)
            component = base / 'camera'
            conv = component / 'conversions'
            conv.mkdir(parents=True)
            (conv / 'jpge.cpp').write_text((CAMERA / 'conversions/jpge.cpp').read_text() if real_encoder else '/* compile-policy fixture */\n')
            (conv / 'jpge.h').write_text((CAMERA / 'conversions/private_include/jpge.h').read_text() if real_encoder else '')
            (conv / 'esp_heap_caps.h').write_text('#pragma once\n')
            for name in ('to_jpg.cpp', 'yuv.c'):
                (conv / name).write_text('/* compile-policy fixture */\n')
            (component / 'driver.c').write_text('int camera_driver_probe(void){return 7;}\n')
            (component / 'CMakeLists.txt').write_text('''add_library(camera STATIC
 conversions/jpge.cpp conversions/to_jpg.cpp conversions/yuv.c driver.c)
target_include_directories(camera PRIVATE conversions)
''')
            (base / 'main.cpp').write_text(r'''
#include "jpge.h"
#include <cstdio>
#include <vector>
#include <cassert>
struct sink : jpge::output_stream {
    std::vector<unsigned char> data;
    jpge::uint get_size() const override {return data.size();}
    bool put_buf(const void *p,int n) override {
        const auto *b=static_cast<const unsigned char *>(p);
        data.insert(data.end(),b,b+n);return true;
    }
};
int main(){
    for(int pattern=0;pattern<3;pattern++){
        sink out;jpge::jpeg_encoder encoder;jpge::params params;
        params.m_quality=80;params.m_subsampling=jpge::H2V2;
        assert(encoder.init(&out,320,240,3,params));
        unsigned char row[320*3];
        for(int y=0;y<240;y++){
            for(int x=0;x<320;x++)for(int c=0;c<3;c++)
                row[x*3+c]=pattern==0 ? 128 : pattern==1 ? (x+y+c*71)%256 : ((x*73+y*157+c*37)^(x*y))%256;
            assert(encoder.process_scanline(row));
        }
        assert(encoder.process_scanline(nullptr));
        assert(out.data.size()>4 && out.data[0]==0xff && out.data[1]==0xd8);
        assert(out.data[out.data.size()-2]==0xff && out.data.back()==0xd9);
        unsigned size=out.data.size();fwrite(&size,sizeof(size),1,stdout);
        fwrite(out.data.data(),1,size,stdout);
    }
}
''')
            if not real_encoder:
                (base / 'main.cpp').write_text('int main(){return 0;}\n')
            cmake = '''cmake_minimum_required(VERSION 3.22)
project(jpeg_regression C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Og)
add_subdirectory(camera)
function(idf_component_get_property out component property)
 if(property STREQUAL "COMPONENT_LIB")
  set(${out} camera PARENT_SCOPE)
 elseif(property STREQUAL "COMPONENT_DIR")
  set(${out} "${CMAKE_CURRENT_SOURCE_DIR}/camera" PARENT_SCOPE)
 endif()
endfunction()
@HOOK@
add_executable(probe main.cpp)
target_include_directories(probe PRIVATE camera/conversions)
target_link_libraries(probe PRIVATE camera)
'''
            outputs = []
            for optimized in (False, True):
                (base / 'CMakeLists.txt').write_text(cmake.replace('@HOOK@', hook if optimized else ''))
                build = base / ('optimized' if optimized else 'baseline')
                subprocess.run(['cmake','-S',str(base),'-B',str(build)],check=True,stdout=subprocess.DEVNULL)
                subprocess.run(['cmake','--build',str(build)],check=True,stdout=subprocess.DEVNULL)
                if optimized:
                    commands=json.loads((build / 'compile_commands.json').read_text())
                    for entry in commands:
                        flags=re.findall(r'(?<!\S)-O\S+',entry['command'])
                        if Path(entry['file']).name in ('jpge.cpp','to_jpg.cpp','yuv.c'):
                            self.assertEqual(flags[-1],'-O2','JPEG hot path must use performance optimization')
                        else:
                            self.assertEqual(flags[-1],'-Og','driver and product debug settings must remain unchanged')
                outputs.append(subprocess.check_output([str(build / 'probe')]))
            if real_encoder:
                self.assertEqual(outputs[0],outputs[1],'optimization must preserve JPEG bytes for all patterns')

    def test_jpeg_optimization_preserves_driver_debug_build(self):
        self.exercise_build(False)

    @unittest.skipUnless((CAMERA / 'conversions/jpge.cpp').is_file(),
                         'optional codec byte comparison requires resolved camera component; build policy is always tested')
    def test_optimized_production_encoder_preserves_jpeg_bytes(self):
        self.exercise_build(True)

if __name__ == '__main__':
    unittest.main()
