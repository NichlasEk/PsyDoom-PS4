#!/usr/bin/env python3
"""Test actual resolver and CUE parser against synthetic, non-game disc files."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='psydoom-disc-') as tmp:
    probe=pathlib.Path(tmp)/'probe'
    main=pathlib.Path(tmp)/'main.cpp'
    main.write_text('#include <cstdio>\n#include <cstddef>\nextern "C" int psydoomFindDisc(const char* const*,size_t,char*,size_t,FILE*);\nint main(int argc,char** argv) { char r[1024]; if(!psydoomFindDisc(argv+1,argc-1,r,sizeof(r),stderr)) return 3; puts(r); }\n')
    subprocess.run(['c++','-std=c++17','-DNDEBUG','-I'+str(root/'baselib'),str(root/'ps4/platform/disc_search.cpp'),str(root/'game/PsyDoom/DiscInfo.cpp'),str(root/'baselib/FileUtils.cpp'),str(main),'-o',str(probe)],check=True)
    roots=[pathlib.Path(tmp)/x for x in ('hdd','usb0','usb1')]
    for p in roots:p.mkdir()
    def disc(p,name='Doom.cue'):
        (p/'track.bin').write_bytes(bytes(2352*32))
        (p/name).write_text('FILE "track.bin" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n')
    def expect(want):
        r=subprocess.run([str(probe),*map(str,roots)],text=True,capture_output=True)
        assert (r.returncode==0 and r.stdout.strip()==str(want)) if want else r.returncode==3, (r.stdout,r.stderr)
    expect(None)
    disc(roots[2],'Other.CUE');expect(roots[2]/'Other.CUE')
    disc(roots[1]);expect(roots[1]/'Doom.cue')
    disc(roots[0]);expect(roots[0]/'Doom.cue')
    (roots[0]/'track.bin').unlink();expect(roots[1]/'Doom.cue')
    (roots[1]/'Doom.cue').write_text('not a cue');expect(roots[2]/'Other.CUE')
    (roots[2]/'track.bin').write_bytes(b'');expect(None)
print('HDD priority, USB0/1 fallback, mixed-case CUE, missing tracks, malformed and empty disc checks passed')
