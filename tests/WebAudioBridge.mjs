// Runs the shipped web wallpaper audio bridge shim against a mock chrome.webview
// transport. Plain node, no browser: the shim is the whole page->host contract for
// hand-authored web wallpapers, and it is far easier to break than to notice.
//
// The test reads the .js file directly, so it always exercises the bytes that the
// CI drift guard asserts are identical to the copy embedded in
// WebDesktopSurfaceChild.cpp.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const shimPath = join(here, '..', 'src', 'desktop', 'wallpaper', 'web',
                      'WallpaperWebAudioBridge.js');
const shimSource = readFileSync(shimPath, 'utf8');
let failures = 0;
function Check(name, fn) {
  try { fn(); console.log(`  [PASS] ${name}`); }
  catch (e) { console.log(`  [FAIL] ${name}\n         ${e.message}`); ++failures; }
}

// 每个用例一个全新 sandbox,避免跨用例污染
function makeSandbox() {
  const handlers = [];
  const sandbox = {
    window: {
      chrome: { webview: { addEventListener: (type, fn) => handlers.push([type, fn]) } }
    },
    isFinite, Number, Array, Object, TypeError, isFinite: globalThis.isFinite
  };
  sandbox.globalThis = sandbox;
  const fn = new Function('window', 'isFinite', 'Number', 'Array', 'Object', 'TypeError',
    `"use strict";\n` + shimSource);
  fn(sandbox.window, isFinite, Number, Array, Object, TypeError);
  return { sandbox, handlers };
}

const frame = (over = {}) => ({
  type: 'audio',
  frame: Object.assign({
    level: 0.5, bands: [0,0,0,0,0], spectrum: new Array(16).fill(0), beat: false
  }, over)
});

console.log('Web wallpaper audio bridge\n');

Check('1. 安装后暴露闭集 API', () => {
  const { sandbox } = makeSandbox();
  const w = sandbox.window.wallpaper;
  assert.ok(w, 'window.wallpaper exists');
  assert.equal(w.version, 1);
  assert.equal(w.bandCount, 5);
  assert.equal(w.spectrumCount, 16);
  assert.equal(typeof w.registerAudioListener, 'function');
  const keys = Object.keys(w).sort();
  assert.deepEqual(keys, ['__miaodeskBridge','bandCount','registerAudioListener','spectrumCount','version'].sort(),
    'API surface is exactly the documented set, no extra callable host surface');
});

Check('2. 注册后收到帧', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  assert.equal(handlers.length, 1, 'one message listener attached');
  handlers[0][1]({ data: frame({ level: 0.75 }) });
  assert.equal(seen.length, 1);
  assert.equal(seen[0].level, 0.75);
  assert.equal(seen[0].bands.length, 5);
  assert.equal(seen[0].spectrum.length, 16);
});

Check('3. 双次注入幂等', () => {
  const { sandbox, handlers } = makeSandbox();
  const marker = sandbox.window.wallpaper;
  // 再把同一份 shim 跑一次(模拟 iframe / 重复导航)
  new Function('window', 'isFinite', `"use strict";\n` + shimSource)(sandbox.window, isFinite);
  assert.equal(sandbox.window.wallpaper, marker, 'second injection did not replace the API');
  assert.equal(handlers.length, 0, 'no listener attached before anyone registers');
  const seen = [];
  sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  assert.equal(handlers.length, 1, 'exactly one message listener after registering');
  handlers[0][1]({ data: frame() });
  assert.equal(seen.length, 1, 'frame delivered exactly once, not twice');
});

Check('4. 退订生效且可重复调用', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  const off = sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  handlers[0][1]({ data: frame() });
  off();
  off(); // 不得抛错
  handlers[0][1]({ data: frame() });
  assert.equal(seen.length, 1, 'no delivery after unsubscribe');
});

Check('5. 多个监听者都收到,退订一个不影响其他', () => {
  const { sandbox, handlers } = makeSandbox();
  const a = [], b = [];
  const offA = sandbox.window.wallpaper.registerAudioListener(f => a.push(f));
  sandbox.window.wallpaper.registerAudioListener(f => b.push(f));
  handlers[0][1]({ data: frame() });
  offA();
  handlers[0][1]({ data: frame() });
  assert.equal(a.length, 1);
  assert.equal(b.length, 2, 'the other listener keeps receiving');
});

Check('6. 抛错的监听者不杀死其他,也不冒泡', () => {
  const { sandbox, handlers } = makeSandbox();
  const good = [];
  sandbox.window.wallpaper.registerAudioListener(() => { throw new Error('author bug'); });
  sandbox.window.wallpaper.registerAudioListener(f => good.push(f));
  handlers[0][1]({ data: frame() });  // 不得抛出
  assert.equal(good.length, 1, 'the healthy listener still ran');
});

Check('7. 同一函数注册两次只投递一次', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  const fn = f => seen.push(f);
  sandbox.window.wallpaper.registerAudioListener(fn);
  sandbox.window.wallpaper.registerAudioListener(fn);
  handlers[0][1]({ data: frame() });
  assert.equal(seen.length, 1, 'duplicate registration is a no-op');
});

Check('8. 畸形帧被丢弃而不是抛出', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  const bad = [undefined, null, {}, { type: 'other' }, { type: 'audio' },
    { type: 'audio', frame: null }, { type: 'audio', frame: {} },
    { type: 'audio', frame: { bands: [0,0,0,0], spectrum: [] } },
    { type: 'audio', frame: { bands: [0,0,0,0,0], spectrum: new Array(15).fill(0) } },
    { type: 'audio', frame: { bands: 'nope', spectrum: new Array(16).fill(0) } },
    { type: 'audio', frame: { bands: new Array(5).fill(0), spectrum: 'nope' } },
    'string', 42, []];
  for (const b of bad) handlers[0][1]({ data: b });
  assert.equal(seen.length, 0, `all ${bad.length} wrong-shape frames dropped`);

  // 多余字段必须被忽略(前向兼容:宿主以后加字段不该破坏旧内容)
  handlers[0][1]({ data: frame({ extraField: 'future' }) });
  assert.equal(seen.length, 1, 'an unknown extra field is ignored, not rejected');

  // 职责划分:宿主保证有限性(B-2 有 2000 帧噪声测试),shim 保证形状。
  // 所以形状错的丢帧,值错的强转为静音 —— 不是丢帧。
  const values = [
    { name: 'all-NaN bands', f: frame({ bands: new Array(5).fill(NaN) }) },
    { name: 'NaN level', f: frame({ level: NaN }) },
    { name: 'undefined entries', f: frame({ bands: [undefined,0,0,0,0] }) },
    { name: 'string entries', f: frame({ bands: ['x',0,0,0,0] }) }
  ];
  for (const { name, f } of values) {
    const before = seen.length;
    handlers[0][1]({ data: f });
    assert.equal(seen.length, before + 1, `${name} is delivered as silence, not dropped`);
    const got = seen[seen.length - 1];
    assert.equal(got.bands.length, 5, `${name}: bands still length 5`);
    for (const b of got.bands) assert.equal(b, 0, `${name}: non-finite coerced to 0`);
    assert.ok(Number.isFinite(got.level), `${name}: level is finite`);
  }
  // 丢完之后信道仍然可用
  const beforeFinal = seen.length;
  handlers[0][1]({ data: frame() });
  assert.equal(seen.length, beforeFinal + 1, 'channel still works after malformed input');
});

Check('9. 越界值被收敛进 [0,1]', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  handlers[0][1]({ data: frame({ level: 5, bands: [-3, 2, 0.5, 0.5, 0.5],
                                 spectrum: new Array(16).fill(99) }) });
  assert.equal(seen[0].level, 1, 'level above 1 clamped');
  assert.equal(seen[0].bands[0], 0, 'negative band clamped to 0');
  assert.equal(seen[0].bands[1], 1, 'band above 1 clamped');
  for (const s of seen[0].spectrum) assert.ok(s >= 0 && s <= 1, 'spectrum in range');
});

Check('10. beat 是布尔沿,不是电平', () => {
  const { sandbox, handlers } = makeSandbox();
  const seen = [];
  sandbox.window.wallpaper.registerAudioListener(f => seen.push(f));
  handlers[0][1]({ data: frame({ beat: false }) });
  handlers[0][1]({ data: frame({ beat: true }) });
  handlers[0][1]({ data: frame({ beat: 'yes' }) });   // truthy 但不是 true
  handlers[0][1]({ data: frame({ beat: 1 }) });
  assert.equal(seen[0].beat, false);
  assert.equal(seen[1].beat, true);
  assert.equal(seen[2].beat, false, 'truthy non-boolean coerced to false, not read as a level');
  assert.equal(seen[3].beat, false);
});

Check('11. 非函数参数被拒', () => {
  const { sandbox } = makeSandbox();
  for (const bad of [undefined, null, 42, 'fn', {}, []])
    assert.throws(() => sandbox.window.wallpaper.registerAudioListener(bad), TypeError);
});

Check('12. 没有 chrome.webview 时注册不抛错(降级为空操作)', () => {
  const handlers = [];
  const win = {};
  new Function('window', 'isFinite', `"use strict";\n` + shimSource)(win, isFinite);
  assert.ok(win.wallpaper, 'API still installed');
  const off = win.wallpaper.registerAudioListener(() => { throw new Error('should not fire'); });
  assert.equal(typeof off, 'function');
  off();  // 不得抛错
});

Check('13. 帧在退订回调内部退订自己不会跳过其他监听者', () => {
  const { sandbox, handlers } = makeSandbox();
  const order = [];
  let offSecond;
  sandbox.window.wallpaper.registerAudioListener(() => order.push('first'));
  offSecond = sandbox.window.wallpaper.registerAudioListener(() => { order.push('second'); offSecond(); });
  sandbox.window.wallpaper.registerAudioListener(() => order.push('third'));
  handlers[0][1]({ data: frame() });
  assert.deepEqual(order, ['first','second','third'], 'snapshot iteration skips no listener');
  handlers[0][1]({ data: frame() });
  assert.deepEqual(order.slice(3), ['first','third'], 'second is gone on the next frame');
});

console.log(`\n${failures ? 'SOME CHECKS FAILED' : 'ALL CHECKS PASSED'} (${failures} failure(s))`);
process.exit(failures ? 1 : 0);
