#!/usr/bin/env python3
"""Fail release validation on cppcheck and conservative Rack pattern findings.

Pattern checks reproduce categories from VCV review #125, not the private
rack-integration-tools implementation. Scan tracked source, not only built modules.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

VERSION = '2.21.0'

def patterns(path, source, root):
    findings = []
    # Blank comments without shifting line numbers; preserve strings for asset paths.
    source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', lambda m: re.sub(r'[^\n]', ' ', m[0]), source)
    def add(kind, pos, message, severity='warning'):
        findings.append(dict(file=str(path), line=source.count('\n', 0, pos)+1,
                             id=kind, severity=severity, message=message))
    for m in re.finditer(r'asset::plugin\s*\(\s*\w+\s*,\s*"(res/[^"\n]+)"', source):
        if not (root/m[1]).is_file():
            add('assetPathMismatch', m.start(), 'Missing asset: '+m[1])
    # Identify method scopes conservatively. Calls through helper functions are
    # flagged too, so resource loading must be visibly inside a draw callback.
    safe = []
    for m in re.finditer(r'\b(?:draw|drawLayer)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{', source):
        depth = 1; end = m.end()
        while end < len(source) and depth:
            depth += (source[end]=='{') - (source[end]=='}'); end += 1
        safe.append((m.end(), end))
    for m in re.finditer(r'\b(?:loadFont|loadImage)\s*\(', source):
        if not any(lo <= m.start() < hi for lo,hi in safe):
            add('fontImageCachedOutsideDraw', m.start(), 'Load fonts/images inside draw/drawLayer; review graphics-context lifetime.')
    for lo,hi in safe:
        for m in re.finditer(r'if\s*\(\s*!module\s*\)\s*(?:\{\s*)?return\s*;', source[lo:hi]):
            add('nullModuleDrawBailout', lo+m.start(), 'Review browser preview: draw returns when module is null.', 'style')
    for m in re.finditer(r'(?<![\w])(?:socket|bind|connect|listen|accept|sendto|recvfrom)\s*\(', source):
        add('networkingApiUsed', m.start(), 'Review possible networking API (may be an unrelated method with the same name).')
    return findings

def is_external_diagnostic(e, roots):
    if e.get('id') in {'syntaxError','internalError','cppcheckError','preprocessorErrorDirective'}:
        return False  # Never accept incomplete analysis, even in an SDK include.
    locations=e.findall('location')
    return bool(locations) and all(any(Path(loc.get('file','')).resolve().is_relative_to(base)
                                     for base in roots) for loc in locations)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cppcheck', default='cppcheck')
    parser.add_argument('--rack-dir', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/release-static'))
    args=parser.parse_args(); root=Path.cwd(); args.output.mkdir(parents=True,exist_ok=True)
    version=subprocess.check_output([args.cppcheck,'--version'],text=True).strip()
    if version != 'Cppcheck '+VERSION: parser.error('Expected Cppcheck '+VERSION+', got '+version)
    sdk=args.rack_dir.resolve()
    for part in ['include/rack.hpp','dep/include']:
        if not (sdk/part).exists(): parser.error('Incomplete Rack SDK: '+str(sdk/part))
    files=sorted(p for p in subprocess.check_output(['git','ls-files','src'],text=True).splitlines() if Path(p).suffix in {'.cpp','.hpp','.h','.cc','.cxx'})
    if not files: parser.error('No tracked source files to scan')
    (args.output/'sources.txt').write_text('\n'.join(files)+'\n')
    # cppcheck 2.21 cannot parse GLEW's parameter name `GLsync GLsync`.
    # Rename only that parameter in an analysis-only copy; retain every type,
    # declaration and function body. The SDK used to compile is never modified.
    overlay=(args.output/'sdk-compat').resolve()
    (overlay/'GL').mkdir(parents=True,exist_ok=True)
    glew=(sdk/'dep/include/GL/glew.h').read_text()
    (overlay/'GL/glew.h').write_text(glew.replace('GLsync GLsync', 'GLsync sync'))
    cmd=[args.cppcheck,*files,'--std=c++11','--max-configs=1','--enable=warning',
         '-I'+str(overlay),'-I'+str(sdk/'include'),'-I'+str(sdk/'dep/include'),'-j','2','-q','--xml','-U_WIN32','-U__APPLE__']
    cmd += ['-D'+n+'(x)=0' for n in ['__has_cpp_attribute','__has_attribute','__has_builtin','__has_extension','__has_feature','__has_warning','__has_include']]
    cmd += ['-D'+n for n in ['__BYTE_ORDER__=1234','__ORDER_LITTLE_ENDIAN__=1234','__ORDER_BIG_ENDIAN__=4321','__linux__=1','__x86_64__=1','__GNUC__=13','__unix__=1']]
    result=subprocess.run(cmd,text=True,capture_output=True)
    (args.output/'cppcheck.xml').write_text(result.stderr)
    (args.output/'cppcheck.stdout.txt').write_text(result.stdout)
    findings=[]
    for filename in files: findings += patterns(filename,Path(filename).read_text(errors='replace'),root)
    (args.output/'patterns.json').write_text(json.dumps(findings,indent=2)+'\n')
    errors=ET.fromstring(result.stderr).findall('./errors/error')
    external_errors=[e for e in errors if is_external_diagnostic(e,(sdk,overlay))]
    errors=[e for e in errors if not is_external_diagnostic(e,(sdk,overlay))]
    lines=[f'{len(external_errors)} SDK-only diagnostics retained in cppcheck.xml (not plugin findings).']
    lines += [f'Cppcheck {VERSION}; {len(files)} tracked source files; exit {result.returncode}']
    for e in errors:
        loc=e.find('location'); location='' if loc is None else f"{loc.get('file')}:{loc.get('line')}"
        lines.append(f"{location}: [{e.get('severity')}] [{e.get('id')}] {e.get('msg')}")
    lines += [f"{f['file']}:{f['line']}: [{f['severity']}] [{f['id']}] {f['message']}" for f in findings]
    report='\n'.join(lines)+'\n';(args.output/'summary.txt').write_text(report);print(report)
    # Errors/warnings block packaging acceptance. Cosmetic style findings remain visible.
    return int(result.returncode != 0 or any(e.get('severity') in {'error','warning'} for e in errors) or any(f['severity']=='warning' for f in findings))

if __name__=='__main__': sys.exit(main())
