#!/usr/bin/env python3
"""Build an isolated local test PKG without commercial disc data."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from check_fself import check_data

ROOT=Path(__file__).resolve().parents[2]
SDK=Path(os.environ.get('OO_PS4_TOOLCHAIN','/opt/openorbis/OpenOrbis/PS4Toolchain'))
TITLE='PSYD00001'
CONTENT='IV0000-PSYD00001_00-PSYDOOMVULKAN001'
ENV=dict(os.environ,OO_PS4_TOOLCHAIN=str(SDK),DOTNET_BUNDLE_EXTRACT_BASE_DIR=str(ROOT/'build/pkgtool-cache'))
TOOL=SDK/'bin/linux/PkgTool.Core'
def run(*args,cwd=None):
    subprocess.run([str(x) for x in args],cwd=cwd,env=ENV,check=True)
def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,default=ROOT/'dist/local')
    args=parser.parse_args()
    dist=args.output_dir.resolve(); dist.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='package-',dir=ROOT/'build'))
    for d in ('sce_sys/about','sce_module','data/licenses'):(stage/d).mkdir(parents=True,exist_ok=True)
    elf=ROOT/'build/ps4/game/PsyDoom'
    needed=subprocess.check_output(['readelf','-d',str(elf)],text=True)
    import re
    deps=re.findall(r'\(NEEDED\).*?\[(.*?)\]',needed)
    allowed={'libkernel.so','libSceNet.so','libSceGnmDriver.so','libSceVideoOut.so','libSceUserService.so','libSceAudioOut.so','libScePad.so','libSceSysmodule.so','libSceSystemService.so'}
    if set(deps)-allowed:raise RuntimeError('Unexpected target imports: '+str(deps))
    run(SDK/'bin/linux/create-fself',f'-in={elf}',f'-out={stage}/PsyDoom.oelf','--eboot',stage/'eboot.bin','--paid','0x3800000000000011')
    check_data(elf,stage/'PsyDoom.oelf')
    for name in ('libc.prx','libSceFios2.prx'):shutil.copyfile(SDK/'samples/SDL2/sce_module'/name,stage/'sce_module'/name)
    shutil.copyfile(SDK/'samples/piglet/sce_sys/about/right.sprx',stage/'sce_sys/about/right.sprx')
    shutil.copyfile(ROOT/'ps4/assets/icon0.png',stage/'sce_sys/icon0.png')
    for src,name in ((ROOT/'LICENSE','PsyDoom.txt'),(ROOT/'ps4/vulkan/UT99-LICENSE.txt','UT99.txt'),(SDK/'LICENSE','OpenOrbis.txt'),(ROOT/'third_party_ps4/vulkan-ps4/LICENSE','Vulkan-PS4.txt'),(ROOT/'third_party_ps4/opengnm/LICENSE','OpenGNM.txt'),(ROOT/'third_party_ps4/opengnm-psbc/LICENSE','PSBC.txt')):
        shutil.copyfile(src,stage/'data/licenses'/name)
    for src,name in (
        (ROOT/'third_party_ps4/SDL-PS4/COPYING.txt','SDL2.txt'),
        (ROOT/'third_party_libs/sol2/sol2/LICENSE.txt','sol2.txt'),
        (ROOT/'third_party_libs/asio/asio/LICENSE_1_0.txt','Asio.txt'),
        (ROOT/'third_party_libs/hash-library/hash-library/LICENSE','hash-library.txt'),
        (ROOT/'third_party_libs/rapidjson/rapidjson/license.txt','RapidJSON.txt')):
        shutil.copyfile(src,stage/'data/licenses'/name)
    sfo=stage/'sce_sys/param.sfo';run(TOOL,'sfo_new',sfo)
    entries={'APP_TYPE':('Integer',4,'1'),'ATTRIBUTE':('Integer',4,'0'),'DOWNLOAD_DATA_SIZE':('Integer',4,'0'),'SYSTEM_VER':('Integer',4,'0'),'APP_VER':('Utf8',8,'0.02'),'VERSION':('Utf8',8,'0.02'),'CATEGORY':('Utf8',4,'gd'),'CONTENT_ID':('Utf8',48,CONTENT),'TITLE_ID':('Utf8',12,TITLE),'TITLE':('Utf8',128,'PsyDoom')}
    for key,(kind,size,value) in entries.items():run(TOOL,'sfo_setentry',sfo,key,'--type',kind,'--maxsize',size,'--value',value)
    files=['eboot.bin']+[str(p.relative_to(stage)) for d in ('sce_sys','sce_module','data') for p in sorted((stage/d).rglob('*')) if p.is_file()]
    run(SDK/'bin/linux/create-gp4','-out','pkg.gp4',f'--content-id={CONTENT}','--files',' '.join(files),cwd=stage)
    tree=ET.parse(stage/'pkg.gp4');rootdir=tree.getroot().find('rootdir');rootdir.clear()
    for name in files:
        node=rootdir
        for part in Path(name).parts[:-1]:
            child=next((c for c in node if c.get('targ_name')==part),None)
            if child is None:child=ET.SubElement(node,'dir',targ_name=part)
            node=child
    tree.write(stage/'pkg.gp4',encoding='utf-8',xml_declaration=True)
    run(TOOL,'pkg_build','pkg.gp4','.',cwd=stage)
    package=dist/'PsyDoom-PS4-0.02.pkg'
    published=package
    package=stage/(CONTENT+'.pkg')
    run(TOOL,'pkg_validate',package)
    extracted=stage/'extracted'
    run(TOOL,'pkg_extract',package,extracted)
    listing=subprocess.check_output([str(TOOL),'pkg_listentries',str(package)],env=ENV,text=True)
    for name,entry in (('param.sfo','PARAM_SFO'),('icon0.png','ICON0_PNG')):
        line=next(line for line in listing.splitlines() if line.split()[-1:]==[entry])
        index=line.split()[3]
        (extracted/'uroot/sce_sys').mkdir(parents=True,exist_ok=True)
        run(TOOL,'pkg_extractentry',package,index,extracted/'uroot/sce_sys'/name)
    for name in files:
        candidate=extracted/'uroot'/name
        if name=='sce_sys/param.sfo':
            import struct
            def sfo_fields(path):
                data=path.read_bytes(); magic,version,keys,values,count=struct.unpack_from('<5I',data)
                if magic!=0x46535000:raise RuntimeError('Invalid SFO magic')
                entries={}
                for i in range(count):
                    key,kind,length,maximum,offset=struct.unpack_from('<HHIII',data,20+16*i)
                    label=data[keys+key:].split(b'\0',1)[0].decode()
                    entries[label]=(kind,data[values+offset:values+offset+length])
                return entries
            original=sfo_fields(stage/name); actual=sfo_fields(candidate)
            if any(actual.get(k)!=v for k,v in original.items()) or set(actual)-set(original)-{'PUBTOOLINFO','PUBTOOLVER'}:
                raise RuntimeError('Packaged SFO fields mismatch')
            continue
        if not candidate.is_file() or digest(candidate)!=digest(stage/name):
            raise RuntimeError('Extracted payload mismatch: '+name)
    run(TOOL,'sfo_listentries',extracted/'uroot/sce_sys/param.sfo')
    shutil.copyfile(package,published)
    package=published
    manifest=dict(payload_readback_verified=True,title_id=TITLE,content_id=CONTENT,app_ver='0.02',stage=str(stage),source_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),dirty=True,elf_sha256=digest(elf),pkg_sha256=digest(package),pkg_bytes=package.stat().st_size,files={name:digest(stage/name) for name in files},runtime_verified=False,disc_data_included=False)
    (dist/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(package)
if __name__=='__main__':main()
