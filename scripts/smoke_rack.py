"""Load the example in an installed Rack host for five seconds, then exit cleanly."""
import pathlib, subprocess, sys
executable, system, user, patch = [pathlib.Path(p).resolve() for p in sys.argv[1:]]
p = subprocess.Popen([str(executable), '-s', str(system), '-u', str(user), '-h', str(patch)],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
try:
    p.wait(timeout=5)
    raise AssertionError(f'Rack exited early: {p.returncode}')
except subprocess.TimeoutExpired:
    output, _ = p.communicate(input=b'\n', timeout=15)
assert p.returncode == 0, output.decode(errors='replace')
log = (user / 'log.txt').read_text()
assert 'Loaded plugin WiggleRoom 2.1.1' in log
for module in ['Wiggle Room ACID9 Voice', 'VCV MIDI to CV', 'VCV Audio 2']:
    assert 'Creating module widget ' + module in log, module
assert 'fatal' not in log.lower() and 'could not find module' not in log.lower()
print('PASS: real Rack installs plugin, loads the MIDI/ACID9/audio example, runs and exits cleanly')
