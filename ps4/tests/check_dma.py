#!/usr/bin/env python3
"""Compile the actual R16 DMA emitter with a bounded host command buffer."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'ps4/vulkan/native/vk_ps4_command.c').read_text()
a=source.index('static bool psydoom_copy_texel_bytes(')
b=source.index('\nVKAPI_ATTR',a)
pre='''#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <pm4/sid.h>
#include <pm4/amdgfxregs.h>
typedef struct Cmd GnmCommandBuffer;
struct Cmd {uint32_t *cmdptr,*endptr; struct {void (*func)(GnmCommandBuffer*,uint32_t,void*);void *userdata;} callback;};
static bool sceGnmDrawCmdCopyMemory(GnmCommandBuffer*c,uint64_t d,uint64_t s,uint32_t n) {return n && !(d&3) && !(s&3) && !(n&3);}
'''
test='''
int main(void) {
 uint32_t words[9]={0}; words[8]=0xdeadbeef;
 GnmCommandBuffer c={words,words+7,{0}};
 assert(psydoom_copy_texel_bytes(&c,0x200000002ull,0x300000006ull,2));
 assert(c.cmdptr==words+7 && words[8]==0xdeadbeef);
 assert(words[0]>>30==3 && ((words[0]>>16)&0x3fff)+2==7);
 assert(words[2]==6 && words[3]==3 && words[4]==2 && words[5]==2);
 assert((words[6]&0x1fffff)==2);
 c.cmdptr=words;c.endptr=words+6;
 assert(!psydoom_copy_texel_bytes(&c,2,6,2));
 assert(c.cmdptr==words);
 assert(!psydoom_copy_texel_bytes(&c,3,6,2));
 assert(!psydoom_copy_texel_bytes(&c,2,6,3));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='psydoom-dma-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(pre+source[a:b]+test)
 subprocess.run(['cc','-std=c11','-I'+str(root/'third_party_ps4/opengnm/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('R16 DMA packet, halfword offsets, bounds and rejection checks passed')
