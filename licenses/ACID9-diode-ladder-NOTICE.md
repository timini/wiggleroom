# ACID9 diode-ladder attribution

The `diode_ladder_zdf` section in `src/modules/ACID9Voice/lib/acid_filter.lib` and the scalar regression reference in `test/test_acid9_filter.py` adapt the diode-ladder equations from Csound `Opcodes/wpfilters.c`, copyright (C) 2017 Steven Yi, licensed LGPL-2.1-or-later. The implementation is based on Will Pirkle’s zero-delay-feedback filter formulation. The adaptation expresses the state update in Faust and fixes the saturation and resonance mapping for ACID9.

Source: https://github.com/csound/csound/blob/develop/Opcodes/wpfilters.c

The LGPL text is included in `LGPL-2.1.txt`. Csound’s work is supplied without warranty, including merchantability or fitness for a particular purpose. WiggleRoom’s surrounding work remains GPL-3.0-or-later.

The accent RC model follows Robin Whittle’s published circuit analysis: https://www.firstpr.com.au/rwi/dfish/303-unique.html. Timing constants and mappings are documented approximations; this voice is not presented as a component-exact TB-303 clone.

The Lead filter uses `lowpassLadder4` from Faust `vaeffects.lib`, by Dario Sanfilippo, MIT License. Faust retains the author and license metadata in the generated header. Source: https://github.com/grame-cncm/faustlibraries/blob/master/vaeffects.lib .

MIT License (Lead ladder implementation, Dario Sanfilippo)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
