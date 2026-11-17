#!/usr/bin/env python3
"""Compile real C++ checks, inspect IR polarity, verify behavior and metadata."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--llvm', type=Path, required=True)
parser.add_argument('--plugin', type=Path, required=True)
parser.add_argument('--beast', type=Path)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--boost', type=Path, default=Path('/opt/homebrew/include'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.out
out.mkdir(parents=True, exist_ok=True)
def run(*cmd):
    result = subprocess.run([str(x) for x in cmd], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout
clang = args.llvm / 'bin/clang++'
opt = args.llvm / 'bin/opt'
core_ir = root/'tests/ir/checks.ll'
for pipeline in ('error-names', 'error-heuristic', 'error-names,beast-errors'):
    run(opt, '-load-pass-plugin', args.plugin, '-passes='+pipeline+',verify', '-S',
        core_ir, '-o', out/'core.ll')
    marked = (out/'core.ll').read_text()
    weights = {i: (int(a), int(b)) for i, a, b in re.findall(
        r'!(\d+) = !\{!"branch_weights", i32 (\d+), i32 (\d+)\}', marked)}
    for name, expected in {'nonzero': (1, 2000), 'zero': (2000, 1),
                            'ordinary': None, 'prior': (123, 456),
                            'uppercase': (1, 2000), 'called_code': (1, 2000),
                            'boolean_true': (1, 2000), 'reversed_xor': (2000, 1),
                            'null_on_left': (2000, 1), 'truncated': None}.items():
        body = re.search(r'define[^\n]*@'+name+r'\([^\n]*\).*?\n}', marked, re.S).group()
        actual = [weights[x] for x in re.findall(r'br i1[^\n]*!prof !(\d+)', body)]
        assert actual == ([] if expected is None else [expected]), (pipeline, name, actual)
run(opt, '-load-pass-plugin', args.plugin, '-passes=beast-errors,verify', '-S',
    core_ir, '-o', out/'effects-only.ll')
assert (out/'effects-only.ll').read_text().count('!prof') == 1
print('PASS: core IR checks and independent rule selection')
if args.beast is None:
    raise SystemExit(0)
common = [clang, '-std=c++17', '-O3', '-g', '-I', args.boost, root/'tests/checks.cpp']
run(*common, '-Xclang', '-disable-llvm-passes', '-S', '-emit-llvm', '-o', out/'raw.ll')
run(opt, '-load-pass-plugin', args.plugin, '-passes=error-heuristic,verify', '-S', out/'raw.ll', '-o', out/'marked.ll')
ir = (out/'marked.ll').read_text()
weights = {
    ident: (int(a), int(b)) for ident, a, b in re.findall(r'!(\d+) = !\{!"branch_weights", i32 (\d+), i32 (\d+)\}', ir)
}
for name, expected in {
    'scalar_error': (1, 2000), 'inverted_error': (2000, 1),
    'negated_error': (1, 2000), 'ec_check': (1, 2000),
    'ec_negated': (1, 2000), 'method_check': (1, 2000),
    'pointer_error': (1, 2000), 'compared_error': (1, 2000), 'specific_error': (2000, 1),
    'uppercase_error': (1, 2000), 'camel_error': (1, 2000),
    'snake_error': (1, 2000), 'camel_method': (1, 2000), 'specific_method': (1, 2000),
    'errorless_check': None, 'ordinary': None, 'terror_check': None, 'unrelated_call': None, 'relational': None,
}.items():
    body = re.search(r'define[^\n]*@'+name+r'\([^\n]*\).*?\n}', ir, re.S).group()
    attached = re.findall(r'br i1[^\n]*!prof !(\d+)', body)
    actual = [weights[x] for x in attached]
    assert actual == ([] if expected is None else [expected]), (name, actual, expected)

# Promoting stack slots turns dbg.declare into dbg.value; names must survive.
run(opt, '-load-pass-plugin', args.plugin, '-passes=mem2reg,error-names,verify', '-S',
    out/'raw.ll', '-o', out/'promoted.ll')
promoted = (out/'promoted.ll').read_text()
for name in ('scalar_error', 'uppercase_error', 'camel_error', 'snake_error'):
    body = re.search(r'define[^\n]*@'+name+r'\([^\n]*\).*?\n}', promoted, re.S).group()
    assert re.search(r'br i1[^\n]*!prof !\d+', body), ('dbg.value', name)

# Prior profile wins, and a shared merge block must remain unmarked.
prior = out/'prior.ll'
prior.write_text('''define i32 @prior(i32 %error) {
entry:
  %c = icmp ne i32 %error, 0
  br i1 %c, label %bad, label %merge, !prof !0
bad:
  br label %merge
merge:
  ret i32 0
}
!0 = !{!"branch_weights", i32 123, i32 456}
''')
run(opt, '-load-pass-plugin', args.plugin, '-passes=error-heuristic,error-heuristic,verify', '-S', prior, '-o', out/'prior-marked.ll')
preserved = (out/'prior-marked.ll').read_text()
assert 'i32 123, i32 456' in preserved and preserved.count('!prof') == 1
run(opt, '-load-pass-plugin', args.plugin, '-passes=error-heuristic,verify', '-S', out/'marked.ll', '-o', out/'twice.ll')
assert (out/'twice.ll').read_text().split('\n', 1)[1] == ir.split('\n', 1)[1]
run(*common, '-o', out/'baseline')
run(*common, '-fpass-plugin='+str(args.plugin), '-o', out/'plugin')
assert run(out/'baseline') == run(out/'plugin')
beast_common = [clang, '-std=c++17', '-O3', '-g', '-I', args.beast/'include', '-I', args.boost, root/'tests/beast_checks.cpp']
run(*beast_common, '-o', out/'beast-baseline')
run(*beast_common, '-fpass-plugin='+str(args.plugin), '-o', out/'beast-plugin')
assert run(out/'beast-baseline') == run(out/'beast-plugin')

assignment_common = [clang, '-std=c++17', '-O3', '-g', '-I', args.beast/'include',
                     '-I', args.boost, root/'tests/assignments.cpp']
run(*assignment_common, '-Xclang', '-disable-llvm-passes', '-S', '-emit-llvm', '-o', out/'assignments-raw.ll')
for label, pipeline in [('extended', 'error-heuristic'), ('names', 'error-names'), ('effects', 'beast-errors')]:
    plugin = args.plugin
    run(opt, '-load-pass-plugin', plugin, '-passes='+pipeline+',verify', '-S',
        out/'assignments-raw.ll', '-o', out/('assignments-'+label+'.ll'))
    marked = (out/('assignments-'+label+'.ll')).read_text()
    run(opt, '-load-pass-plugin', plugin, '-passes='+pipeline+',verify', '-S',
        out/('assignments-'+label+'.ll'), '-o', out/('assignments-'+label+'-twice.ll'))
    assert (out/('assignments-'+label+'-twice.ll')).read_text().split('\n', 1)[1] == marked.split('\n', 1)[1]
    ws = {i: (int(a), int(b)) for i, a, b in re.findall(
        r'!(\d+) = !\{!"branch_weights", i32 (\d+), i32 (\d+)\}', marked)}
    for name, expected in {
        'assigns_bad': (1, 2000), 'assigns_else': (2000, 1),
        'assigns_direct': (1, 2000), 'assigns_service': None,
        'assigns_buffer': None, 'assigns_chunk': None, 'assigns_eof': None,
        'assigns_clear': None, 'assigns_shared': None, 'constructs_only': None,
        'assigns_unknown': None, 'assigns_both': None,
    }.items():
        body = re.search(r'define[^\n]*@'+name+r'\([^\n]*\).*?\n}', marked, re.S).group()
        actual = [ws[i] for i in re.findall(r'br i1[^\n]*!prof !(\d+)', body)]
        wanted = [] if label == 'names' or expected is None else [expected]
        assert actual == wanted, (label, name, actual, wanted)
    run(clang, '-O3', out/('assignments-'+label+'.ll'), '-o', out/('assignments-'+label))
run(*assignment_common, '-o', out/'assignments-baseline')
expected_output = run(out/'assignments-baseline')
assert run(out/'assignments-extended') == expected_output == run(out/'assignments-names') == run(out/'assignments-effects')
# Debug-free enum constants must not be guessed from their numeric values.
run(*[x for x in assignment_common if x != '-g'], '-Xclang', '-disable-llvm-passes',
    '-S', '-emit-llvm', '-o', out/'assignments-no-debug.ll')
run(opt, '-load-pass-plugin', args.plugin, '-passes=error-heuristic,verify', '-S',
    out/'assignments-no-debug.ll', '-o', out/'assignments-no-debug-marked.ll')
no_debug = (out/'assignments-no-debug-marked.ll').read_text()
for name in ('assigns_bad', 'assigns_else', 'assigns_direct', 'assigns_service'):
    body = re.search(r'define[^\n]*@'+name+r'\([^\n]*\).*?\n}', no_debug, re.S).group()
    assert '!prof' not in body, name
print('PASS: 19 C++ IR cases, 12 assignment cases for all three rule selections, debug-free enum handling, preserved profile, idempotence, executable checks for 201 inputs, 9 valid/invalid Beast messages')
