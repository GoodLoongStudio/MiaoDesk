// 把 C++ 产出的音频信封,交给真的 shim 跑一遍。
//
// 为什么值得:这是 B-6 契约唯一"两份实现之间"的检查。tests/WebAudioBridge.mjs 验的是
// 页面侧 normalizeFrame() 对**手写**帧的判断;src/tests/WebAudioEnvelope.cpp 验的是
// 宿主侧信封的形状。两份都绿,不等于这两份字节能对上 —— 而一旦对不上,表现是
// "壁纸什么都收不到",没有任何错误信息,因为 shim 的丢弃路径是静默 return。
//
// 于是这里让 C++ 真的打印它的输出,再用 node 加载真的 shim,把那些字节当
// chrome.webview 的 message 投递进去,看注册的监听者到底收到了什么。
// 也就是说:被测的是**真实产出**,不是任何一方的重写。
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
const shimPath = join(repo, 'src', 'desktop', 'wallpaper', 'web', 'WallpaperWebAudioBridge.js');
const shimSource = readFileSync(shimPath, 'utf8');

let failures = 0;
function Check(name, fn) {
  try { fn(); console.log(`  [PASS] ${name}`); }
  catch (e) { console.log(`  [FAIL] ${name}\n         ${e.message}`); ++failures; }
}

// 与 tests/WebAudioBridge.mjs 同一个 sandbox 形状:shim 读的是 window.chrome.webview。
function loadShim() {
  const handlers = [];
  const sandbox = {
    window: { chrome: { webview: { addEventListener: (t, fn) => handlers.push([t, fn]) } } }
  };
  sandbox.globalThis = sandbox;
  const fn = new Function('window', 'isFinite', 'Number', 'Array', 'Object', 'TypeError',
    `"use strict";\n` + shimSource);
  fn(sandbox.window, isFinite, Number, Array, Object, TypeError);
  return { sandbox, handlers };
}

// --- 1. 编出 C++ 侧那个打印器 -------------------------------------------------
console.log('编译 src/include/miaodesk/WallpaperWebAudioEnvelope.h 的真实产出…');
// 自己建缓存目录:一次全新的 clone 上没有 node_modules/,而把它杵在那儿等失败,
// 报出来的会是 ENOENT 而不是"缺目录"。
const cacheDir = join(repo, 'node_modules', '.cache');
import { mkdirSync } from 'node:fs';
mkdirSync(cacheDir, { recursive: true });
const binary = join(cacheDir, 'web-audio-envelope-dump');
const compile = spawnSync('c++', ['-std=c++20', `-I${join(repo, 'src', 'include')}`, '-o', binary,
  join(repo, 'tests', 'WebAudioEnvelopeDump.cpp')], { encoding: 'utf8' });
if (compile.status !== 0) {
  console.log(`  [FAIL] 编译失败(需要 c++ 与 C++20)\n${compile.stderr}`);
  process.exit(1);
}

const out = execFileSync(binary, { encoding: 'utf8' }).split('\n').filter(l => l.length > 0);
if (out.length % 2 !== 0) {
  console.log(`  [FAIL] 输出必须是"输入行/信封行"成对出现,实得 ${out.length} 行`);
  process.exit(1);
}

const pairs = [];
for (let i = 0; i < out.length; i += 2) {
  pairs.push({ input: JSON.parse(out[i]), envelope: JSON.parse(out[i + 1]) });
}
console.log(`取到 ${pairs.length} 组 C++ 真实样本\n`);

// --- 2. 逐组过真 shim ---------------------------------------------------------
console.log('1. 信封被 shim 接受,且监听者收到的值就是发出去的值');

for (const [index, pair] of pairs.entries()) {
  const { sandbox, handlers } = loadShim();
  const received = [];
  // attach() 是惰性的:只有 registerAudioListener 被调用时,shim 才去摸
  // window.chrome.webview。所以顺序必须是先注册,再取 message 监听者 ——
  // 第一版反了,于是一条都取不到。
  const unsubscribe = sandbox.window.wallpaper.registerAudioListener(f => received.push(f));
  const message = handlers.filter(([type]) => type === 'message');

  // 先投递,再断言。第一版把投递写在了"退订"那个用例里,于是前两个用例看到的
  // 永远是 0 帧 —— 测的是我自己的顺序错,不是契约。
  for (const [, fn] of message) fn({ data: pair.envelope });

  Check(`样本 ${index + 1}:信封被接受(没有在 normalizeFrame 被丢弃)`, () => {
    if (message.length === 0) throw new Error('shim 没有注册 message 监听者');
    if (received.length !== 1) {
      throw new Error(`监听者收到 ${received.length} 帧,期望 1 —— `
        + `信封: ${out[index * 2 + 1]}`);
    }
  });

  Check(`样本 ${index + 1}:收到的字段值与输入一致(四位量化内)`, () => {
    const f = received[0];
    const want = pair.input;
    const near = (a, b) => Math.abs(a - b) <= 5e-5;   // 四位小数的一半,含 round
    const cmp = (label, got, exp) => {
      if (!near(got, exp)) {
        throw new Error(`${label}: 收到 ${got},发出 ${exp}(信封 ${out[index * 2 + 1]})`);
      }
    };
    if (typeof f.beat !== 'boolean') throw new Error(`beat 不是布尔: ${typeof f.beat}`);
    if (f.beat !== want.beat) throw new Error(`beat: 收到 ${f.beat},发出 ${want.beat}`);
    if (!Array.isArray(f.bands) || f.bands.length !== want.bands.length) {
      throw new Error(`bands 长度: ${f.bands && f.bands.length} != ${want.bands.length}`);
    }
    if (!Array.isArray(f.spectrum) || f.spectrum.length !== want.spectrum.length) {
      throw new Error(`spectrum 长度: ${f.spectrum && f.spectrum.length} != ${want.spectrum.length}`);
    }
    cmp('level', f.level, want.level);
    for (let i = 0; i < want.bands.length; ++i) cmp(`bands[${i}]`, f.bands[i], want.bands[i]);
    for (let i = 0; i < want.spectrum.length; ++i) cmp(`spectrum[${i}]`, f.spectrum[i], want.spectrum[i]);
  });

  // B-6 验收标准里的一条:退订之后不再收到。上面已经证明订阅能收到,这里证明退订有效。
  Check(`样本 ${index + 1}:退订后不再收到`, () => {
    unsubscribe();
    for (const [, fn] of message) fn({ data: pair.envelope });
    if (received.length !== 1) {
      throw new Error(`退订后又收到 ${received.length - 1} 帧`);
    }
  });
}

// --- 3. 契约的另一半:两边对"多少个桶"的认知 ----------------------------------
console.log('\n2. C++ 与 shim 对频段/频谱条数的认知一致');
Check('shim 声明的条数与 C++ 发出来的条数相同', () => {
  const { sandbox } = loadShim();
  const declared = sandbox.window.wallpaper.spectrumCount;
  const sent = pairs[0].envelope.frame.spectrum.length;
  if (declared !== sent) {
    throw new Error(`shim 声明 spectrumCount=${declared},C++ 发 ${sent} 条`);
  }
});
Check('bands 条数与 shim 的 BAND_COUNT 一致', () => {
  const sent = pairs[0].envelope.frame.bands.length;
  if (sent !== 5) throw new Error(`C++ 发 ${sent} 条 bands,shim 期望 5`);
  // 反过来也查一次:shim 会因为长度不符而整帧丢弃,所以这个数必须两边都对。
  const { sandbox, handlers } = loadShim();
  const got = [];
  sandbox.window.wallpaper.registerAudioListener(f => got.push(f));
  for (const [, fn] of handlers.filter(([t]) => t === 'message')) fn({ data: pairs[0].envelope });
  if (got.length !== 1) throw new Error('5 条 bands 被 shim 丢弃了');
});

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : 'FAILED'} (${failures} 处失败)`);
process.exit(failures === 0 ? 0 : 1);
