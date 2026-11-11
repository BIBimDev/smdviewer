"""Field-by-field oracle comparison; no third-party Python modules required."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ABS_TOL, REL_TOL = 1e-9, 1e-12


def compare(actual, expected, path='$'):
    if isinstance(expected, dict):
        assert isinstance(actual, dict) and actual.keys() == expected.keys(), f'{path}: field presence differs'
        for key in expected:
            compare(actual[key], expected[key], f'{path}.{key}')
    elif isinstance(expected, list):
        assert isinstance(actual, list) and len(actual) == len(expected), f'{path}: array size differs'
        for i, value in enumerate(expected):
            compare(actual[i], value, f'{path}[{i}]')
    elif isinstance(expected, bool) or expected is None or isinstance(expected, str):
        assert type(actual) is type(expected) and actual == expected, f'{path}: {actual!r} != {expected!r}'
    elif isinstance(expected, int):
        # All integer-valued fields, including flags and coordinates, compare exactly.
        assert isinstance(actual, (int, float)) and not isinstance(actual, bool) and actual == expected, f'{path}: integer differs'
    else:
        assert isinstance(actual, (int, float)) and not isinstance(actual, bool), f'{path}: number type differs'
        assert math.isfinite(actual) and math.isclose(actual, expected, abs_tol=ABS_TOL, rel_tol=REL_TOL), f'{path}: {actual} != {expected}'


def run(executable, path, expected):
    result = subprocess.run([executable, str(path)], capture_output=True, text=True, encoding='utf-8')
    if 'error' in expected:
        assert result.returncode == 1, f'{path}: error exit status {result.returncode}'
        assert result.stdout == '', f'{path}: error wrote stdout'
        assert result.stderr == expected['error'] + '\n', f'{path}: error message differs: {result.stderr!r}'
    else:
        assert result.returncode == 0 and result.stderr == '', f'{path}: {result.stderr}'
        def invalid_constant(value):
            raise AssertionError(f'invalid JSON constant {value}')
        actual = json.loads(result.stdout, parse_constant=invalid_constant)
        compare(actual, expected)
        return actual


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--extract', required=True)
    args.add_argument('--tokens', required=True)
    options = args.parse_args()
    provenance = json.loads((HERE / 'golden/provenance.json').read_text())
    for name, expected_hash in provenance['sha256'].items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == expected_hash, f'Reference source changed: {name}'
    for name, vertex_count, face_count, transformed in [('plain_column', 8, 6, True), ('pocket_column', 16, 10, False)]:
        expected = json.loads((HERE / f'golden/{name}.json').read_text(encoding='utf-8'))
        actual = run(options.extract, ROOT / f'public/samples/{name}.smd', expected)
        first_output = subprocess.check_output([options.extract, str(ROOT / f'public/samples/{name}.smd')])
        assert first_output == subprocess.check_output([options.extract, str(ROOT / f'public/samples/{name}.smd')]), 'nondeterministic JSON'
        brep = actual['parsed']['geometry']['brep']
        assert len(brep['verticesLocal']) == vertex_count and len(brep['faces']) == face_count
        assert ('transform' in brep) == transformed
        for axis, low, high in [('x', -375, 375), ('y', -325, 325), ('z', -1000, 5000)]:
            assert min(v[axis] for v in brep['verticesWorld']) == low
            assert max(v[axis] for v in brep['verticesWorld']) == high
    behavior = json.loads((HERE / 'golden/behavior.json').read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='smd-regression-') as temp:
        path = Path(temp) / 'input.smd'
        for group, executable in [('cases', options.extract), ('tokenCases', options.tokens)]:
            for case in behavior[group]:
                path.write_bytes(case['input'].encode('utf-8'))
                try:
                    run(executable, path, case['expected'])
                except Exception as error:
                    raise AssertionError(f'{case["id"]}: {error}') from error
        missing = subprocess.run([options.extract, str(Path(temp) / 'missing.smd')], capture_output=True)
        assert missing.returncode == 1 and missing.stdout == b'' and missing.stderr
        usage = subprocess.run([options.extract], capture_output=True)
        assert usage.returncode == 2 and usage.stdout == b'' and b'Usage:' in usage.stderr
        # Additional native byte-boundary rule, not a TypeScript parser error.
        path.write_bytes(b'"DICAD ASC"\n#10125\n\xc0\xaf')
        invalid_utf8 = subprocess.run([options.extract, str(path)], capture_output=True)
        assert invalid_utf8.returncode == 1 and invalid_utf8.stdout == b''
        assert invalid_utf8.stderr == b'SMD text must be valid UTF-8.\n'
    print(f'PASS: both full specimen goldens, {len(behavior["cases"])} parser cases, '
          f'{len(behavior["tokenCases"])} tokenizer cases, CLI errors; abs={ABS_TOL}, rel={REL_TOL}')


if __name__ == '__main__':
    main()
