// Shim tipis di atas bridge root WebUI. KernelSU, APatch, dan manager
// kompatibel-MMRL semuanya menyediakan `window.ksu.exec(cmd, options, cb)`
// dengan bentuk yang sama (KernelSU WebUIX convention). Kalau tidak ada
// bridge sama sekali (dibuka di browser biasa), kita kasih tahu user.

const DENYLIST_FILE = "/data/adm/isolpolicy/denylist.conf";
const SELF_PKG = "io.github.mhmrdd.isolationpolicy";

let cbCounter = 0;
function runShell(cmd) {
  return new Promise((resolve, reject) => {
    if (typeof ksu === "undefined" || typeof ksu.exec !== "function") {
      reject(new Error("Tidak ada bridge root WebUI (window.ksu). Buka halaman ini dari dalam Manager (KernelSU/APatch), bukan browser biasa."));
      return;
    }
    const cbName = "__isolpolicy_cb_" + (cbCounter++);
    window[cbName] = (errno, stdout, stderr) => {
      delete window[cbName];
      if (errno !== 0) {
        reject(new Error(stderr || ("exit code " + errno)));
      } else {
        resolve(stdout || "");
      }
    };
    ksu.exec(cmd, "{}", cbName);
  });
}

const state = {
  installed: [],   // {pkg, label}
  denied: new Set(),
  filter: "",
};

async function loadInstalledPackages() {
  // pm list packages -3 -> daftar paket pihak ketiga saja (bukan sistem)
  const raw = await runShell("pm list packages -3 | sed 's/^package://'");
  return raw
    .split("\n")
    .map((s) => s.trim())
    .filter(Boolean)
    .sort();
}

async function loadDenylist() {
  try {
    const raw = await runShell(`cat ${DENYLIST_FILE} 2>/dev/null || true`);
    return new Set(
      raw
        .split("\n")
        .map((s) => s.trim())
        .filter((s) => s && !s.startsWith("#"))
    );
  } catch (e) {
    return new Set();
  }
}

async function saveDenylist(denySet) {
  const lines = [
    "# Satu nama paket per baris. Baris kosong / diawali '#' diabaikan.",
    "# Paket modul ini sendiri selalu di-deny secara internal.",
    ...Array.from(denySet).filter((p) => p !== SELF_PKG),
    "",
  ].join("\n");
  // Tulis lewat heredoc supaya aman dari karakter aneh di nama paket
  // (nama paket Android valid selalu alnum + titik, jadi ini cukup aman).
  const escaped = lines.replace(/'/g, "'\\''");
  await runShell(`cat > ${DENYLIST_FILE} <<'EOF'\n${escaped}\nEOF\nchmod 644 ${DENYLIST_FILE}`);
}

function renderList() {
  const container = document.getElementById("list");
  const filterLower = state.filter.toLowerCase();
  const shown = state.installed.filter(
    (item) =>
      item.pkg.toLowerCase().includes(filterLower) ||
      item.label.toLowerCase().includes(filterLower)
  );

  if (shown.length === 0) {
    container.innerHTML = '<div class="status">Tidak ada aplikasi yang cocok</div>';
    return;
  }

  container.innerHTML = "";
  for (const item of shown) {
    const row = document.createElement("div");
    row.className = "row";

    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = state.denied.has(item.pkg);
    cb.addEventListener("change", () => {
      if (cb.checked) state.denied.add(item.pkg);
      else state.denied.delete(item.pkg);
    });

    const meta = document.createElement("div");
    meta.className = "meta";
    meta.innerHTML = `<div class="label">${item.label}</div><div class="pkg">${item.pkg}</div>`;
    meta.addEventListener("click", () => cb.click());

    row.appendChild(cb);
    row.appendChild(meta);
    container.appendChild(row);
  }
}

async function init() {
  const listEl = document.getElementById("list");
  try {
    const [installed, denied] = await Promise.all([
      loadInstalledPackages(),
      loadDenylist(),
    ]);
    state.installed = installed.map((pkg) => ({ pkg, label: pkg }));
    state.denied = denied;
    renderList();
  } catch (e) {
    listEl.innerHTML = `<div class="status">Gagal memuat: ${e.message}</div>`;
  }
}

document.getElementById("search").addEventListener("input", (e) => {
  state.filter = e.target.value;
  renderList();
});

document.getElementById("apply").addEventListener("click", async () => {
  const btn = document.getElementById("apply");
  const status = document.getElementById("applyStatus");
  btn.disabled = true;
  status.textContent = "Menyimpan…";
  try {
    await saveDenylist(state.denied);
    status.textContent = "Tersimpan. Reboot supaya perubahan aktif.";
  } catch (e) {
    status.textContent = "Gagal menyimpan: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

init();
