import base64
import atexit
import shutil
import tempfile
import subprocess
import json
import time
import urllib.request
from pathlib import Path

import argparse
parser = argparse.ArgumentParser(description='Headless Firefox acceptance checks; start geckodriver separately.')
parser.add_argument('--url', default='http://127.0.0.1:5174/')
parser.add_argument('--driver', default='http://127.0.0.1:4445')
parser.add_argument('--session', help='Reuse an existing test session (otherwise create and close one)')
parser.add_argument('--extract', required=True, help='Native smd-extract executable')
parser.add_argument('--screenshots', default='/tmp', help='Directory for evidence PNGs')
args = parser.parse_args()
BASE = args.driver + ('/session/' + args.session if args.session else '')
ROOT = Path(__file__).resolve().parent.parent
SHARED = Path(tempfile.mkdtemp(prefix='.geo00-int02-browser-', dir=ROOT))
atexit.register(shutil.rmtree, SHARED)
for name in ['plain_column', 'pocket_column']:
    (SHARED / (name + '.native.json')).write_bytes(subprocess.check_output([args.extract, str(ROOT / 'public/samples' / (name + '.smd'))]))
ELEMENT = 'element-6066-11e4-a52e-4f735466cecf'

def call(path, data=None, method=None):
    req = urllib.request.Request(BASE + path, None if data is None else json.dumps(data).encode(),
        {'Content-Type': 'application/json'}, method=method)
    try:
        result = json.load(urllib.request.urlopen(req, timeout=30))['value']
    except urllib.error.HTTPError as error:
        raise RuntimeError(error.read().decode())
    return result

def js(script, *args):
    return call('/execute/sync', {'script': script, 'args': list(args)})

def element(selector):
    return call('/element', {'using': 'css selector', 'value': selector})[ELEMENT]

def click(selector):
    if '-row-' in selector: selector += ' td'
    js('document.querySelector(arguments[0]).scrollIntoView({block: "center"})', selector)
    call('/element/' + element(selector) + '/click', {})

def state():
    return js('''return {
      status: document.querySelector('#statusText').textContent,
      vertices: document.querySelector('#vertexCountBadge').textContent,
      faces: document.querySelector('#faceCountBadge').textContent,
      vertexRows: document.querySelector('#vertexTableBody').innerHTML,
      faceRows: document.querySelector('#faceTableBody').innerHTML,
      geometry: document.querySelector('#geometryInfo').innerHTML,
      object: [...document.querySelectorAll('#objectInfo dd')].slice(1).map(e=>e.textContent),
      details: document.querySelector('#selectedFaceDetails').textContent,
      selected: document.querySelectorAll('tr.is-selected, tr.is-vertex-selected, tr.is-face-vertex').length,
      canvases: document.querySelectorAll('canvas').length
    }''')

def load(path, native=True, success=True):
    selector = '#nativeJsonInput' if native else '#fileInput'
    call('/element/' + element(selector) + '/value', {'text': str(path)})
    for _ in range(100):
        s = state()
        if s['status'].startswith(('Loaded ', 'Could not load ')):
            break
        time.sleep(.05)
    assert s['status'].startswith('Loaded ' if success else 'Could not load '), s
    assert ('Native C++ (JSON v1)' if native else 'Native C++ (WASM)') in s['status'], s
    return s

def wait_for(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError('Timed out: ' + str(state()))

def observe_errors():
    js('window.__smokeErrors=[]; window.addEventListener("error",e=>window.__smokeErrors.push(e.message)); window.addEventListener("unhandledrejection",e=>window.__smokeErrors.push(String(e.reason)));')

def fresh_page():
    call('/url', {'url': args.url})
    wait_for(lambda: js('return !!document.querySelector("#fileInput")'))
    observe_errors()

def file_event(data, name='synthetic.smd', drop=False):
    js('''
      const transfer = new DataTransfer();
      transfer.items.add(new File([new Uint8Array(arguments[0])], arguments[1]));
      if (arguments[2]) document.querySelector('#viewer').dispatchEvent(new DragEvent('drop', {dataTransfer: transfer, bubbles: true, cancelable: true}));
      else {
        const input = document.querySelector('#fileInput');
        input.files = transfer.files;
        input.dispatchEvent(new Event('change', {bubbles: true}));
      }
    ''', list(data), name, drop)
    return wait_for(lambda: state() if state()['status'].startswith(('Loaded ', 'Could not load ')) else None)

def screenshot(name):
    data = call('/element/' + element('#viewer') + '/screenshot')
    (Path(args.screenshots) / (name + '.png')).write_bytes(base64.b64decode(data))

if not args.session:
    session = call('/session', {'capabilities': {'alwaysMatch': {
        'browserName': 'firefox', 'moz:firefoxOptions': {'args': ['-headless'],
        'prefs': {'webgl.force-enabled': True, 'webgl.software': True}}}}})
    BASE = args.driver + '/session/' + session['sessionId']
    atexit.register(lambda: call('', method='DELETE'))
call('/window/rect', {'width': 1440, 'height': 1000})
call('/url', {'url': args.url})
time.sleep(.5)
observe_errors()

for name, vertices, faces in [('plain_column', '8', '6'), ('pocket_column', '16', '10')]:
    smd = load(ROOT / 'public/samples' / (name + '.smd'), False)
    assert (smd['vertices'], smd['faces']) == (vertices, faces)
    native = load(str(SHARED / (name + '.native.json')))
    for key in ['vertices', 'faces', 'vertexRows', 'faceRows', 'geometry', 'object']:
        assert native[key] == smd[key], (name, key)
    assert native['canvases'] == 2
    for _ in range(2):
        load(ROOT / 'public/samples' / (name + '.smd'), False)
        repeated = load(str(SHARED / (name + '.native.json')))
        assert repeated['vertexRows'] == native['vertexRows']
        assert repeated['faceRows'] == native['faceRows']
    print('PASS both input paths and repeated switching:', name, flush=True)

    for native_input in [False, True]:
        load(str(SHARED / (name + '.native.json')) if native_input else ROOT / 'public/samples' / (name + '.smd'), native_input)
        click('#fitButton')
        click('#verticesButton')
        assert js('return document.querySelector("#verticesButton").getAttribute("aria-pressed")') == 'true'
        click('#vertex-row-0')
        assert js('return document.querySelector("#vertex-row-0").classList.contains("is-vertex-selected")')
        click('#faceSelectButton')
        click('#face-row-0')
        assert 'Selected face F0' in state()['details']
        click('#faceColorsButton')
        click('#transparencyButton')
        assert js('return document.querySelector("#renderModeStatus").textContent') == 'Transparent'
        click('#viewCubeButton')
        assert js('return document.querySelector("#viewCubeButton").getAttribute("aria-pressed")') == 'false'
        click('#viewCubeButton')
        if name == 'pocket_column' and native_input:
            screenshot('geo00-int02-pocket-inspector')
        click('#verticesButton')
        click('#faceSelectButton')
        click('#faceColorsButton')
        click('#transparencyButton')
        assert state()['selected'] == 0
    screenshot('geo00-int02-' + name)
    print('PASS controls and selection for both sources:', name, flush=True)

plain = json.loads((SHARED / 'plain_column.native.json').read_text())
invalids = [('malformed', '{'), ('schema', json.dumps({**plain, 'schemaVersion': 2}))]
for name, mutate in [
    ('index', lambda e: e['parsed']['geometry']['brep']['faces'][0]['indices'].__setitem__(0, 999)),
    ('null', lambda e: e['parsed']['geometry']['brep']['verticesWorld'][0].__setitem__('x', None)),
]:
    value = json.loads(json.dumps(plain))
    mutate(value)
    invalids.append((name, json.dumps(value)))
for name, text in invalids:
    path = SHARED / ('invalid-' + name + '.json')
    path.write_text(text)
    click('#faceSelectButton')
    click('#face-row-0')
    rejected = load(path, success=False)
    assert rejected['vertices'] == rejected['faces'] == '0'
    assert rejected['geometry'] == '' and rejected['selected'] == 0
    assert rejected['details'] == ''
    assert 'Native JSON:' in rejected['status']
    recovered = load(SHARED / 'pocket_column.native.json')
    assert recovered['vertices'] == '16'
    click('#faceSelectButton')
    print('PASS rejection, stale-state clearing and recovery:', name, flush=True)

# Filename and metadata remain text even when they contain HTML.
special = SHARED / '<img src=x onerror=alert(1)>.native.json'
special.write_text((SHARED / 'plain_column.native.json').read_text())
assert '<img src=x onerror=alert(1)>' in load(special)['status']
assert js('return document.querySelectorAll("#objectInfo img, #statusText img").length') == 0

# Delayed reads must not replace a newer load; the older read may also fail.
for reject in [False, True]:
    js('''
      const original = File.prototype.arrayBuffer;
      File.prototype.arrayBuffer = function() {
        if (this.name.startsWith('plain_column')) {
          return new Promise((resolve,reject)=>setTimeout(()=>{
            if(arguments[0]) reject(Error('late read error'));
            else original.call(this).then(resolve,reject);
          },500));
        }
        return original.call(this);
      };
      window.__restoreRead=()=>{File.prototype.arrayBuffer=original;};
    '''.replace('if(arguments[0])', 'if(' + json.dumps(reject) + ')'))
    call('/element/' + element('#nativeJsonInput') + '/value', {'text': str(SHARED / 'plain_column.native.json')})
    load(SHARED / 'pocket_column.native.json')
    time.sleep(.7)
    assert state()['vertices'] == '16'
    assert 'pocket_column' in state()['status']
    js('window.__restoreRead()')
print('PASS escaped filename and overlapping reads', flush=True)
assert js('return window.__smokeErrors') == []
print('PASS no window errors or unhandled promise rejections', flush=True)

# File/drop input, actual high-bit browser decoding, SMD rejection and recovery.
plain_bytes = (ROOT / 'public/samples/plain_column.smd').read_bytes()
raw_name = plain['parsed']['geometryBlockLines'][3].encode('ascii')
high_bytes = plain_bytes.replace(raw_name, b'"St\xfctze \x80 \x93\xe9\x94"', 1)
assert 'Native C++ (WASM)' in file_event(high_bytes)['status']
assert 'Stütze € “é”' in js('return document.querySelector("#objectInfo").textContent')
assert file_event(plain_bytes, 'drop.smd', True)['vertices'] == '8'
for data in [b'', b'"OTHER"\n#10125\n', b'"DICAD ASC"\n#10071\n', b'"DICAD ASC"\n#10125\n1\n']:
    rejected = file_event(data, '<bad & input>.smd')
    assert 'WASM parse error:' in rejected['status'], rejected
    assert rejected['vertices'] == rejected['faces'] == '0'
    assert rejected['geometry'] == '' and rejected['details'] == ''
    assert js('return document.querySelectorAll("#objectInfo bad, #statusText bad").length') == 0
    assert load(ROOT / 'public/samples/plain_column.smd', False)['vertices'] == '8'
print('PASS SMD drop, high-bit Windows-1252 decoding, malformed/unsupported input and recovery', flush=True)

# The same initialized module parses subsequent local files without HTTP requests.
js('performance.clearResourceTimings()')
load(ROOT / 'public/samples/pocket_column.smd', False)
load(ROOT / 'public/samples/plain_column.smd', False)
assert js('return performance.getEntriesByType("resource").filter(e=>e.initiatorType !== "other").map(e=>e.name)') == []
print('PASS local SMD parsing causes no network requests after static WASM retrieval', flush=True)

for name, count in [('plain_column', 8), ('pocket_column', 16)]:
    load(ROOT / 'public/samples' / (name + '.smd'), False)
    click('#verticesButton')
    time.sleep(.15)
    visible = 'return [...document.querySelectorAll(".vertex-label")].filter(e=>getComputedStyle(e).display !== "none").length'
    solid = js(visible)
    click('#transparencyButton')
    wait_for(lambda: js(visible) == count)
    assert 0 < solid < count, (name, solid, count)
    click('#transparencyButton')
    wait_for(lambda: js(visible) == solid)
    click('#verticesButton')
print('PASS solid hides occluded vertices; transparency exposes all vertices in both specimens', flush=True)

# Exercise the real cube pointer listener, including drag; compare rendered output.
before = call('/element/' + element('#viewer') + '/screenshot')
rect = js('const r=document.querySelector(".view-cube-canvas").getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}')
x, y = round(rect['x'] + rect['w'] * .5), round(rect['y'] + rect['h'] * .35)
call('/actions', {'actions': [{'type': 'pointer', 'id': 'cube', 'parameters': {'pointerType': 'mouse'}, 'actions': [
    {'type': 'pointerMove', 'duration': 0, 'x': x, 'y': y}, {'type': 'pointerDown', 'button': 0},
    {'type': 'pointerUp', 'button': 0}]}]})
time.sleep(.25)
snapped = call('/element/' + element('#viewer') + '/screenshot')
assert snapped != before
call('/actions', {'actions': [{'type': 'pointer', 'id': 'cube', 'parameters': {'pointerType': 'mouse'}, 'actions': [
    {'type': 'pointerMove', 'duration': 0, 'x': x, 'y': y}, {'type': 'pointerDown', 'button': 0},
    {'type': 'pointerMove', 'duration': 300, 'x': x + 25, 'y': y + 15}, {'type': 'pointerUp', 'button': 0}]}]})
time.sleep(.2)
assert call('/element/' + element('#viewer') + '/screenshot') != snapped
click('#fitButton')
print('PASS cube pointer snap and drag change the rendered view; fit remains usable', flush=True)
assert js('return window.__smokeErrors') == []

# Delay the first WASM fetch, then choose JSON while initialization is pending.
# Both an older success and older failure must leave that newer model intact.
for reject in [False, True]:
    fresh_page()
    js('''
      const original = window.fetch;
      window.__restoreFetch=()=>{window.fetch=original;};
      window.fetch = function(...args) {
        if (String(args[0]).endsWith('smd-parser.wasm')) {
          if(window.__wasmRejected) return Promise.reject(Error('synthetic delayed init failure'));
          window.__wasmWaiting=true;
          return new Promise((resolve,reject)=>{
            window.__releaseWasm=()=>{
              if (arguments[0]) {window.__wasmRejected=true;reject(Error('synthetic delayed init failure'));}
              else {window.fetch=original;original.apply(window,args).then(resolve,reject);}
            };
          });
        }
        return original.apply(window,args);
      };
    '''.replace('if (arguments[0])', 'if (' + json.dumps(reject) + ')'))
    call('/element/' + element('#fileInput') + '/value', {'text': str(ROOT / 'public/samples/plain_column.smd')})
    wait_for(lambda: js('return window.__wasmWaiting'))
    load(SHARED / 'pocket_column.native.json')
    js('window.__releaseWasm()')
    time.sleep(.7)
    assert state()['vertices'] == '16' and 'Native C++ (JSON v1)' in state()['status'], state()
    js('window.__restoreFetch()')
    assert load(ROOT / 'public/samples/plain_column.smd', False)['vertices'] == '8'
    assert js('return window.__smokeErrors') == []
print('PASS pending WASM initialization success/failure cannot overwrite newer JSON; retry succeeds', flush=True)

# Current initialization failure must clear stale geometry, visibly fail, and
# recover on retry. A TS fallback would incorrectly succeed on this valid input.
fresh_page()
load(SHARED / 'pocket_column.native.json')
js('''const original=window.fetch;window.__restoreFetch=()=>{window.fetch=original;};window.fetch=function(...args){
  if(String(args[0]).endsWith('smd-parser.wasm')){
    return Promise.reject(Error('<synthetic WASM unavailable>'));
  }return original.apply(window,args);
};''')
failed = load(ROOT / 'public/samples/plain_column.smd', False, False)
assert 'WASM initialization failed:' in failed['status'], failed
assert failed['vertices'] == failed['faces'] == '0' and failed['geometry'] == ''
assert js('return document.querySelectorAll("#objectInfo synthetic").length') == 0
js('window.__restoreFetch()')
assert load(ROOT / 'public/samples/plain_column.smd', False)['vertices'] == '8'
assert js('return window.__smokeErrors') == []
print('PASS initialization failure is visible, clears stale model, has no TS fallback, and retries', flush=True)
print('Firefox WASM acceptance automation passed:', args.url, flush=True)
