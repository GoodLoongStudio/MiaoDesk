import assert from 'node:assert/strict';
import http from 'node:http';
import { once } from 'node:events';
import { pathToFileURL } from 'node:url';

const requests = [];
const png = Buffer.from('89504e470d0a1a0a0000000d49484452', 'hex').toString('base64');

const server = http.createServer(async (req, res) => {
  let body = '';
  for await (const chunk of req) body += chunk;
  requests.push({
    method: req.method,
    url: req.url,
    authorization: req.headers.authorization ?? '',
    contentType: req.headers['content-type'] ?? '',
    body: body ? JSON.parse(body) : {},
  });
  if (req.url === '/v1/images/generations') {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ data: [{ b64_json: png }] }));
    return;
  }
  res.writeHead(404, { 'content-type': 'application/json' });
  res.end(JSON.stringify({ error: { message: 'not found' } }));
});
server.listen(0, '127.0.0.1');
await once(server, 'listening');

try {
  const address = server.address();
  assert.equal(typeof address, 'object');
  const baseUrl = `http://127.0.0.1:${address.port}/v1`;

  process.env.MIAODESK_IMAGE_PROVIDER = 'local-openai-compatible';
  process.env.MIAODESK_IMAGE_BASE_URL = baseUrl;
  process.env.MIAODESK_IMAGE_MODEL = 'Z-Image-Turbo';
  process.env.MIAODESK_IMAGE_API_KEY = '';

  const extractor = await import('./extract-image-openai-shim.mjs');
  const shim = await import(pathToFileURL(extractor.default).href + `?t=${Date.now()}`);

  assert.equal(shim.usesOpenAICompatibleImageShim('local-openai-compatible'), true);
  assert.equal(shim.usesOpenAICompatibleImageShim('openai-compatible'), true);
  assert.equal(shim.usesOpenAICompatibleImageShim('openrouter'), false);
  assert.equal(shim.openAIImageEndpoint(baseUrl), baseUrl + '/images/generations');

  const key = await shim.resolveImageApiKey();
  assert.equal(key, 'local');

  const image = await shim.generateOpenAICompatibleImage('blue glass clock widget', 'Z-Image-Turbo', key);
  assert.equal(image.data, png);
  assert.equal(image.mimeType, 'image/png');
  assert.equal(requests.length, 1);

  const req = requests[0];
  assert.equal(req.method, 'POST');
  assert.equal(req.url, '/v1/images/generations');
  assert.equal(req.authorization, '', 'loopback key sentinel must not be sent as Authorization');
  assert.match(req.contentType, /^application\/json/);
  assert.equal(req.body.model, 'Z-Image-Turbo');
  assert.equal(req.body.prompt, 'blue glass clock widget');
  assert.equal(req.body.n, 1);
  assert.equal(req.body.response_format, 'b64_json');

  console.log('ALL CHECKS PASSED: OpenAI-compatible local image shim request/response contract');
} finally {
  server.close();
}
