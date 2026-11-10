#!/usr/bin/env python3
"""Read-only source/dependency inventory; writes a local JSON report."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def git(path, *args):
    result = subprocess.run(['git', '-C', str(path), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stack', type=Path, required=True)
    args = parser.parse_args()
    stack = args.stack.resolve()
    if not stack.is_dir():
        parser.error(f'Stack directory missing: {stack}')
    symbols = {}
    for folder in ('vulkan_gl', 'game/PsyDoom/Vulkan', 'vulkan_shaders'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.suffix not in ('.cpp', '.h', '.vert', '.frag', '.glsl'):
                continue
            for number, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                for name in set(re.findall(r'\b(?:vk[A-Z]\w*|VK_FORMAT_\w+|SDL_Vulkan_\w+)\b', line)):
                    symbols.setdefault(name, []).append(f'{path.relative_to(ROOT)}:{number}')
    dependencies = []
    for name, archive in (('vulkan-ps4', 'libvulkan_ps4.a'), ('opengnm', 'libopengnm.a'), ('opengnm-psbc', 'libpsbc.orbis.a')):
        directory = stack / name
        binary = directory / archive
        dependencies.append(dict(name=name, path=str(directory), revision=git(directory, 'rev-parse', 'HEAD'),
                                 status=git(directory, 'status', '--short'), archive_exists=binary.is_file(),
                                 archive_sha256=hashlib.file_digest(binary.open('rb'), 'sha256').hexdigest() if binary.is_file() else None))
    output = ROOT / 'artifacts/ps4/inventory.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(dict(scope='Static inventory only; support and runtime unverified',
                                     source_revision=git(ROOT, 'rev-parse', 'HEAD'), dependencies=dependencies,
                                     symbols=symbols), indent=2) + '\n')
    print(f'{output}: {len(symbols)} symbols, {len(dependencies)} dependency records')

if __name__ == '__main__':
    main()
