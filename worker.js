// Web Worker — loads the WASM module and bridges UCI I/O.
// No pthreads / SharedArrayBuffer needed.

importScripts("limerikk_wasm.js");

let Module = null;
let uci_send  = null;
let uci_flush = null;

LimerikkUCI({
    print:    (msg) => console.log("[engine]", msg),
    printErr: (msg) => console.warn("[engine]", msg),
}).then((mod) => {
    Module = mod;
    uci_send  = mod.cwrap("uci_send",  null,     ["string"]);
    uci_flush = mod.cwrap("uci_flush", "string", []);

    mod.ccall("uci_init", null, [], []);

    self.postMessage({ type: "ready" });
}).catch((err) => {
    console.error("[worker] WASM init failed:", err);
    self.postMessage({ type: "error", message: String(err) });
});

self.onmessage = (e) => {
    if (!uci_send) return;

    if (e.data.type === "send") {
        uci_send(e.data.cmd);
        // Flush any output produced synchronously by this command.
        const out = uci_flush();
        if (out) {
            for (const line of out.split("\n").filter(l => l.length > 0)) {
                self.postMessage({ type: "output", line });
            }
        }
    }
};
