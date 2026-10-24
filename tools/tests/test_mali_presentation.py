from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cc'), 'needs a C compiler')
class MaliPresentationTest(unittest.TestCase):
    def test_device_extensions_keep_shm_path_without_dmabuf(self):
        source = (ROOT / 'app/src/main/cpp/waylandcomp/src/vk_present.c').read_text()
        start = source.index('    const char *dev_exts[7]')
        end = source.index('    int want_hdr_md = 0;', start)
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"
typedef struct { char extensionName[128]; } VkExtensionProperties;
static int g_pd, g_dev_state, g_dmabuf_import, mask;
static const char *extensions[] = {"VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf",
 "VK_EXT_image_drm_format_modifier", "VK_KHR_image_format_list"};
static void enumerate(int device, void *layer, uint32_t *n, VkExtensionProperties *out) {
 (void)device; (void)layer;
 if (!out) { *n = 4; return; }
 *n = 0;
 for (int i=0;i<4;i++) if (mask & (1<<i)) strcpy(out[(*n)++].extensionName, extensions[i]);
}
static struct { void (*EnumerateDeviceExtensionProperties)(int,void*,uint32_t*,VkExtensionProperties*); } g_vk = {enumerate};
static int has_ext(VkExtensionProperties *ext, unsigned n, const char *name) {
 for (unsigned i=0;i<n;i++) if (!strcmp(ext[i].extensionName,name)) return 1;
 return 0;
}
#define droiddeck_log(...) ((void)0)
static int select_extensions(void) {
''' + source[start:end] + r'''
 assert(!strcmp(dev_exts[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME));
 assert(n_dev_exts == (mask == 15 ? 5 : 1));
 assert(g_dmabuf_import == (mask == 15));
 free(exts);
 return 0;
}
int main(void) {
 for (mask=0;mask<16;mask++) assert(select_extensions()==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            c = Path(folder) / 'check.c'
            exe = Path(folder) / 'check'
            c.write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)
