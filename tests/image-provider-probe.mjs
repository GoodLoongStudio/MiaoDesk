// Runs the extracted logic in a child process so the module-level process.env reads
// see the target environment before import.
const mod = await import('./extracted-image-provider.mts');
const { calls } = await import('./stub-get-image-model.mjs');
const out = {
  provider: mod.IMAGE_PROVIDER,
  model: mod.IMAGE_MODEL || mod.DEFAULT_IMAGE_MODEL,
  key: await mod.resolveImageApiKey(),
};
const baseUrl = await mod.currentMiaoDeskBaseUrl();
out.baseUrl = baseUrl;
out.loopback = mod.isLoopback(baseUrl.toLowerCase());
try {
  await mod.generateImage('prompt', 'file.png');
  out.threw = 'NO THROW';
} catch (error) {
  out.threw = error instanceof Error ? error.message : String(error);
  out.reachedGetImageModel = out.threw.startsWith('STUB:');
}
out.getImageModelArgs = calls[0] ?? null;
process.stdout.write(JSON.stringify(out));
