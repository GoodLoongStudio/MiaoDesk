// Runtime capability probe for the exact @earendil-works/pi-ai bytes staged into MiaoDesk.
// This intentionally performs no network request. It asks the installed compat registry
// which image providers/models it knows, so product provider IDs are never assumed compatible.
const label = process.argv[2] || "unknown-runtime";

let compat;
try {
  compat = await import("@earendil-works/pi-ai/compat");
} catch (error) {
  console.error(`[FAIL] ${label}: cannot import @earendil-works/pi-ai/compat: ${error?.stack || error}`);
  process.exit(2);
}

const keys = Object.keys(compat).sort();
const getProviders = compat.getImageProviders;
const getModels = compat.getImageModels;
const getModel = compat.getImageModel;

if (typeof getProviders !== "function" || typeof getModels !== "function" || typeof getModel !== "function") {
  console.error(`[FAIL] ${label}: compat image registry API is incomplete. exports=${keys.join(",")}`);
  process.exit(3);
}

function providerId(value) {
  if (typeof value === "string") return value;
  if (!value || typeof value !== "object") return String(value ?? "");
  return String(value.id ?? value.provider ?? value.providerId ?? value.name ?? "");
}

function modelId(value) {
  if (typeof value === "string") return value;
  if (!value || typeof value !== "object") return String(value ?? "");
  return String(value.id ?? value.model ?? value.modelId ?? value.name ?? "");
}

let rawProviders;
try {
  rawProviders = await getProviders();
} catch (error) {
  console.error(`[FAIL] ${label}: getImageProviders() failed: ${error?.stack || error}`);
  process.exit(4);
}
const providerValues = Array.isArray(rawProviders) ? rawProviders : [...(rawProviders ?? [])];
const providers = providerValues.map(providerId).filter(Boolean);

const details = [];
for (const provider of providers) {
  let rawModels = [];
  try {
    rawModels = await getModels(provider);
  } catch (error) {
    details.push({ provider, error: String(error?.message || error), modelCount: 0, models: [] });
    continue;
  }
  const modelValues = Array.isArray(rawModels) ? rawModels : [...(rawModels ?? [])];
  const models = modelValues.map(modelId).filter(Boolean);
  let firstModelResolves = null;
  if (models.length) {
    try {
      firstModelResolves = Boolean(getModel(provider, models[0]));
    } catch {
      firstModelResolves = false;
    }
  }
  details.push({ provider, modelCount: models.length, models: models.slice(0, 12), firstModelResolves });
}

const candidateIds = [
  "openrouter",
  "openai-compatible",
  "local-openai-compatible",
  "google",
  "local",
];
const candidates = Object.fromEntries(candidateIds.map(id => [
  id,
  providers.some(p => p.toLowerCase() === id.toLowerCase()),
]));

const report = {
  label,
  node: process.version,
  compatExports: keys,
  providers,
  candidates,
  details,
};
console.log("MIAODESK_IMAGE_PROVIDER_CAPABILITIES " + JSON.stringify(report));
