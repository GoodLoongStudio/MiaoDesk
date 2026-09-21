// Records what getImageModel was asked for instead of touching a real provider.
export const calls = [];
export function getImageModel(provider, model) {
  calls.push([provider, model]);
  return { provider, model };
}
