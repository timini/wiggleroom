"""Verify Rack's zstd/tar package, scope, required assets and actual binary CPU."""
import io, json, pathlib, struct, subprocess, sys, tarfile
package, platform = pathlib.Path(sys.argv[1]), sys.argv[2]
assert package.read_bytes()[:4] == bytes.fromhex('28b52ffd'), 'Not a zstd Rack package'
raw = subprocess.check_output(['zstd', '-d', '-c', str(package)])
with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
    names = {n.rstrip('/') for n in archive.getnames()}
    assert all(n == 'WiggleRoom' or n.startswith('WiggleRoom/') for n in names)
    assert all('..' not in pathlib.PurePosixPath(n).parts for n in names)
    def read(name):
        return archive.extractfile('WiggleRoom/' + name).read()
    manifest = json.loads(read('plugin.json'))
    assert manifest['version'] == '2.1.1'
    assert [m['slug'] for m in manifest['modules']] == ['ACID9Voice']
    for name in ['LICENSE', 'res/ACID9Voice.png', 'res/ACID9Voice-labels.svg',
                 'docs/user/modules/ACID9Voice.md', 'presets/ACID9Voice/Classic acid.vcvm']:
        assert read(name), name
    assert not any(n.startswith('WiggleRoom/res/') and 'ACID9Voice' not in n and n != 'WiggleRoom/res' for n in names)
    if platform.startswith('mac'):
        binary = read('plugin.dylib')
        assert binary[:4] == bytes.fromhex('cffaedfe'), 'Expected thin Mach-O'
        cpu = struct.unpack_from('<I', binary, 4)[0]
        assert cpu == (0x100000c if platform == 'mac-arm64' else 0x1000007)
    elif platform == 'lin-x64':
        binary = read('plugin.so')
        assert binary[:5] == b'\x7fELF\x02' and struct.unpack_from('<H', binary, 18)[0] == 62
    else:
        binary = read('plugin.dll')
        assert binary[:2] == b'MZ'
        offset = struct.unpack_from('<I', binary, 60)[0]
        assert binary[offset:offset+4] == b'PE\0\0' and struct.unpack_from('<H', binary, offset+4)[0] == 0x8664
print(f'PASS {package.name}: zstd/tar, ACID9-only manifest, assets, {platform} binary')
