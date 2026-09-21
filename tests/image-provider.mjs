// Verifies image_generate's provider parameterisation (P0-2).
//
// The logic under test is EXTRACTED from src/ai/pi/PiNativeToolsExtension.cpp at
// run time — see extract-image-provider.mjs — so this exercises the shipped bytes
// rather than a copy that can drift. The old defect it pins: getImageModel was
// called with a hardcoded "openrouter" and the key was reused only when baseUrl
// contained "openrouter.ai", so a profile pointing at a local inference server made
// image generation fail unconditionally.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

// pathToFileURL is required, not decoration: `import()` treats its argument as a URL
// specifier. On POSIX an absolute path happens to be accepted, but on Windows
// "D:\a\MiaoDesk\tests\..." is parsed as a URL with protocol "d:" and fails with
// ERR_UNSUPPORTED_ESM_URL_SCHEME. This gate is Windows-only in CI, so the POSIX-only
// behaviour hid the bug locally.
import { pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const extractScript = join(here, 'extract-image-provider.mjs');
const probe = join(here, 'image-provider-probe.mjs');

// Write the extracted module before any probe imports it.
await import(pathToFileURL(extractScript).href);

let failures = 0;
function Check(name, fn) {
  try { fn(); console.log(`  [PASS] ${name}`); }
  catch (e) { console.log(`  [FAIL] ${name}\n         ${e.message}`); ++failures; }
}

// Whether this node still needs --experimental-strip-types. Type stripping became
// default in Node 22.18, and the flag survives as a no-op there — but relying on
// that is how "works locally, fails on CI" happens: the runner's node version is not
// the one on the dev machine. Probe once and pass the flag only when it is accepted.
let stripTypesFlags;
try {
  execFileSync(process.execPath, ['--experimental-strip-types', '-e', '0'], { stdio: 'ignore' });
  stripTypesFlags = ['--experimental-strip-types'];
} catch {
  stripTypesFlags = [];
}

async function withEnv(env) {
  const agentDir = await mkdtemp(join(tmpdir(), 'imgprov-'));
  await writeFile(join(agentDir, 'models.json'),
                  JSON.stringify({ providers: { miaodesk: { baseUrl: env.baseUrl ?? '' } } }), 'utf8');
  const childEnv = {
    ...process.env,
    PI_CODING_AGENT_DIR: agentDir,
    MIAODESK_IMAGE_PROVIDER: env.provider ?? '',
    MIAODESK_IMAGE_MODEL: env.model ?? '',
    MIAODESK_MODEL_API_KEY: env.mainKey ?? '',
    MIAODESK_IMAGE_API_KEY: env.imageKey ?? '',
  };
  const out = execFileSync(process.execPath,
      [...stripTypesFlags, probe],
      { env: childEnv, encoding: 'utf8' });
  return JSON.parse(out);
}

console.log('image provider 参数化 (P0-2)\n');

console.log('1. 未配置 provider -> 明确失败,不是静默回落云端');
{
  const r = await withEnv({ baseUrl: 'https://api.openai.com/v1' });
  Check('provider 为空', () => assert.equal(r.provider, ''));
  Check('不发 key', () => assert.equal(r.key, ''));
  Check('明确报"未配置"而非静默回落云端', () =>
    assert.match(r.threw, /未配置：API Profile 未指定图片 Provider/));
}

console.log('\n2. openrouter profile(向后兼容的默认路径)');
{
  const r = await withEnv({ provider: 'openrouter', baseUrl: 'https://openrouter.ai/api/v1', mainKey: 'sk-main' });
  Check('provider 透传', () => assert.equal(r.provider, 'openrouter'));
  Check('model 未配时回落默认', () => assert.equal(r.model, 'google/gemini-2.5-flash-image'));
  Check('复用主 key', () => assert.equal(r.key, 'sk-main'));
  Check('getImageModel 收到 openrouter', () =>
    assert.deepEqual(r.getImageModelArgs, ['openrouter', 'google/gemini-2.5-flash-image']));
}

console.log('\n3. 本地推理端点(loopback)—— 这是原来必然失效的那条');
{
  const r = await withEnv({ provider: 'local', baseUrl: 'http://127.0.0.1:8000/v1', mainKey: 'sk-main' });
  Check('provider 透传', () => assert.equal(r.provider, 'local'));
  Check('识别为 loopback', () => assert.equal(r.loopback, true));
  Check('loopback 不需要 key(发哨兵值而非空)', () => assert.equal(r.key, 'local'));
  Check('走到了 getImageModel', () => assert.equal(r.reachedGetImageModel, true));
  Check('getImageModel 收到 (local, 默认模型)', () =>
    assert.deepEqual(r.getImageModelArgs, ['local', 'google/gemini-2.5-flash-image']));
}

console.log('\n4. 其他云厂商 profile');
{
  const r = await withEnv({ provider: 'google', baseUrl: 'https://generativelanguage.googleapis.com/v1beta/openai', mainKey: 'sk-g' });
  Check('provider 透传', () => assert.equal(r.provider, 'google'));
  Check('复用主 key', () => assert.equal(r.key, 'sk-g'));
}

console.log('\n5. 专用 image key 优先于主 key');
{
  const r = await withEnv({ provider: 'openrouter', baseUrl: 'https://openrouter.ai/api/v1', mainKey: 'sk-main', imageKey: 'sk-image' });
  Check('image key 优先', () => assert.equal(r.key, 'sk-image'));
}

console.log('\n6. model 可覆盖');
{
  const r = await withEnv({ provider: 'local', model: 'Z-Image-Turbo', baseUrl: 'http://127.0.0.1:8188/v1' });
  Check('model 覆盖生效', () => assert.equal(r.model, 'Z-Image-Turbo'));
  Check('getImageModel 收到覆盖后的 model', () =>
    assert.deepEqual(r.getImageModelArgs, ['local', 'Z-Image-Turbo']));
}

console.log('\n7. loopback 判定覆盖各种写法');
for (const url of ['http://127.0.0.1:8000/v1', 'http://localhost:8000/v1',
                   'http://[::1]:8000/v1', 'https://LOCALHOST:8000/v1']) {
  const r = await withEnv({ provider: 'local', baseUrl: url, mainKey: 'sk' });
  Check(`loopback: ${url}`, () => assert.equal(r.loopback, true));
}
for (const url of ['https://openrouter.ai/api/v1', 'http://192.168.1.50:8000/v1']) {
  const r = await withEnv({ provider: 'x', baseUrl: url, mainKey: 'sk' });
  Check(`非 loopback: ${url}`, () => assert.equal(r.loopback, false));
}

// The node version and platform ride along in the summary line on purpose: CI's
// annotation channel only surfaces lines matching an error-ish pattern, and
// "FAILED" is the one reliable way to get the environment into a line that shows up
// when the gate fails. Without it a Windows-only failure reads as a bare exit code.
console.log(`\n${failures ? 'SOME CHECKS FAILED' : 'ALL CHECKS PASSED'} (${failures} failure(s)) ` +
            `node=${process.version} platform=${process.platform}/${process.arch}`);
process.exit(failures ? 1 : 0);
