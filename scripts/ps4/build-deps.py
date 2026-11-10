#!/usr/bin/env python3
"""Verify source snapshots and build the native PS4 libraries locally."""
import hashlib,json,os,pathlib,shutil,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[2]
sdk=pathlib.Path(os.environ.get('OO_PS4_TOOLCHAIN','/opt/openorbis/OpenOrbis/PS4Toolchain')).resolve()
source=root/'third_party_ps4'; lock=source/'sources.lock.json'
manifest=json.loads(lock.read_text())
for name,want in manifest['files'].items():
    p=source/name
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=want:
        sys.exit('Dependency source does not match sources.lock.json: '+name)
if not (sdk/'lib/libSDL2.a').is_file():sys.exit('Set OO_PS4_TOOLCHAIN to the OpenOrbis SDK')
work=root/'build/ps4-deps'; native=work/'native';work.mkdir(parents=True,exist_ok=True)
sdk_hash=hashlib.sha256()
for path in sorted((sdk/'lib').glob('*.a')):
    sdk_hash.update(path.name.encode());sdk_hash.update(hashlib.sha256(path.read_bytes()).digest())
stamp=work/'build-id';fingerprint=hashlib.sha256(lock.read_bytes()+pathlib.Path(__file__).read_bytes()+sdk_hash.digest()+str(sdk).encode()+subprocess.check_output(['clang','--version'])).hexdigest()
outputs=[native/'opengnm/libopengnm.a',native/'opengnm-psbc/libpsbc.orbis.a',native/'vulkan-ps4/libvulkan_ps4.a']
if stamp.exists() and stamp.read_text()==fingerprint and all(p.is_file() for p in outputs):sys.exit(0)
# Never reuse object files from a different snapshot or toolchain.
if native.exists():shutil.rmtree(native)
for name in manifest['components']:
    if name!='SDL-PS4':shutil.copytree(source/name,native/name)
env=dict(os.environ,OO_PS4_TOOLCHAIN=str(sdk));jobs=os.environ.get('BUILD_JOBS','2')
def run(args,folder):subprocess.run(args,cwd=native/folder,env=env,check=True)
run(['make','lib','CC=clang','AR=llvm-ar'], 'opengnm')
run(['make','-f','Makefile.orbis','-j'+jobs], 'opengnm-psbc')
run(['make','-f','Makefile.orbis','-j'+jobs,'libvulkan_ps4.a'], 'vulkan-ps4')
stamp.write_text(fingerprint)
