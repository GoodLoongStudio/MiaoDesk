// MiaoDesk web wallpaper audio bridge.
//
// Injected into every web wallpaper by WebDesktopSurfaceChild. Hand-authored web
// content is the only consumer: the AI/skill authoring path never produces web
// content, so this API exists for users writing their own HTML.
//
// The contract is deliberately one-directional and closed:
//   * host -> page: audio frames, and nothing else
//   * page -> host: nothing. The page cannot ask the host to do anything.
//
// That is not a limitation of WebView2, it is the security posture: the web surface
// already denies navigation, dev tools, context menus, new windows and every
// permission request. Exposing a callable host surface would undo that, so the only
// thing the page gets is data.
//
// `window.chrome.webview` is the transport; this shim is the stable API in front of
// it, so the transport can change without breaking authored content.

(function () {
  "use strict";

  // Guard against double injection. AddScriptToExecuteOnDocumentCreated runs on
  // every navigation and every iframe, so a naive shim would register the message
  // listener twice and deliver every frame twice.
  if (typeof window === "undefined" || !window) return;
  if (window.wallpaper && window.wallpaper.__miaodeskBridge) return;

  var BRIDGE_VERSION = 1;
  var BAND_COUNT = 5;
  var SPECTRUM_COUNT = 16;

  var listeners = [];
  var attached = false;

  // Clamp into [0, 1] and treat anything that is not a finite number as silence.
  //
  // This deliberately coerces rather than rejects. The division of responsibility is:
  // the host guarantees every value is finite (covered by the analyzer's own tests),
  // and this shim guarantees the documented *shape* so author code can rely on it
  // without defensive checks. An earlier version tried to reject a non-finite value
  // here, but because unit() already returned 0 the rejection was unreachable dead
  // code — it read as validation while doing coercion.
  function unit(value) {
    if (typeof value !== "number" || !isFinite(value)) return 0;
    if (value < 0) return 0;
    if (value > 1) return 1;
    return value;
  }

  // Returns null only when the array is the wrong shape (missing, wrong length, not
  // array-like). Individual bad *values* are coerced by unit(), not rejected.
  function numericArray(value, expectedLength) {
    if (!value || typeof value.length !== "number" || value.length !== expectedLength) return null;
    var out = [];
    for (var i = 0; i < expectedLength; ++i) out.push(unit(value[i]));
    return out;
  }

  // Normalise a raw host frame into the documented shape. Returns null when the
  // frame is not an audio frame at all, which the caller treats as "drop silently":
  // the host may legitimately send other message types on the same transport later.
  function normalizeFrame(data) {
    if (!data || typeof data !== "object") return null;
    if (data.type !== "audio") return null;
    var raw = data.frame;
    if (!raw || typeof raw !== "object") return null;

    var bands = numericArray(raw.bands, BAND_COUNT);
    if (!bands) return null;
    var spectrum = numericArray(raw.spectrum, SPECTRUM_COUNT);
    if (!spectrum) return null;

    return {
      level: unit(raw.level),
      bands: bands,
      spectrum: spectrum,
      // `beat` is a single-frame edge, not a level. Coercing with !! keeps a stray
      // truthy value from being read as a level.
      beat: raw.beat === true
    };
  }

  function onMessage(event) {
    var frame = normalizeFrame(event && event.data);
    if (!frame) return;
    // Copy before iterating: a listener may unregister itself or another, and
    // mutating the array mid-loop would skip a subscription.
    var snapshot = listeners.slice();
    for (var i = 0; i < snapshot.length; ++i) {
      try {
        snapshot[i](frame);
      } catch (error) {
        // One broken listener must not stop the others, and must not surface as an
        // unhandled error that WebView2 would report as a page failure.
      }
    }
  }

  function attach() {
    if (attached) return;
    var transport = window.chrome && window.chrome.webview;
    if (!transport || typeof transport.addEventListener !== "function") return;
    transport.addEventListener("message", onMessage);
    attached = true;
  }

  function registerAudioListener(listener) {
    if (typeof listener !== "function") {
      throw new TypeError("wallpaper.registerAudioListener expects a function");
    }
    if (listeners.indexOf(listener) !== -1) {
      // Registering the same function twice would deliver each frame twice.
      return function () {};
    }
    listeners.push(listener);
    attach();

    var removed = false;
    return function unsubscribe() {
      if (removed) return;
      removed = true;
      var index = listeners.indexOf(listener);
      if (index !== -1) listeners.splice(index, 1);
    };
  }

  window.wallpaper = {
    __miaodeskBridge: true,
    version: BRIDGE_VERSION,
    bandCount: BAND_COUNT,
    spectrumCount: SPECTRUM_COUNT,
    registerAudioListener: registerAudioListener
  };
})();
