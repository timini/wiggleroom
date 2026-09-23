#!/usr/bin/env python3
"""Give generated Faust scalar/array state explicit zero initializers.

Run after Faust generation, before committing the header. DSP init() still sets
sample-rate constants, controls and state; this also makes pre-init objects safe
and is understood by cppcheck without suppressing uninitialized-member checks.
"""
import re
import sys
from pathlib import Path

def initialize(text):
    start=text.index('class mydsp : public dsp {')
    end=text.index(' public:',start)
    body=text[start:end]
    body,count=re.subn(r'^(\s*(?:int|float|double|FAUSTFLOAT)\s+\w+(?:\[\d+\])?)\s*;',r'\1 = {};',body,flags=re.M)
    if not count and '= {};' not in body:
        raise ValueError('Unexpected Faust member declarations; inspect generated class')
    return text[:start]+body+text[end:]

if __name__=='__main__':
    p=Path(sys.argv[1]);p.write_text(initialize(p.read_text()))
