// Shim tipis di atas bridge root WebUI. KernelSU, APatch, dan manager
// kompatibel-MMRL semuanya menyediakan `window.ksu.exec(cmd, options, cb)`
// dengan bentuk yang sama (KernelSU WebUIX convention). Kalau tidak ada
// bridge sama sekali (dibuka di browser biasa), kita kasih tahu user.

const DENYLIST_FILE = "/data/adm/isolpolicy/denylist.conf";
const SELF_PKG = "io.github.mhmrdd.isolationpolicy";
const MODULE_ID = "isolpolicy_zygisk";

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
  // Tulis ke file sementara dulu lalu mv, konsisten dengan pola yang dipakai
  // action.sh, supaya companion process (yang memantau lewat inotify)
  // selalu melihat file utuh saat event IN_CLOSE_WRITE muncul, bukan isi
  // yang sedang setengah ditulis.
  await runShell(
    `TMP="${DENYLIST_FILE}.webui.tmp"; ` +
    `cat > "$TMP" <<'EOF'\n${escaped}\nEOF\n` +
    `mv "$TMP" "${DENYLIST_FILE}" && chmod 644 "${DENYLIST_FILE}"`
  );
}

// ---------------------------------------------------------------------------
// Status reload live: modul ini mendeteksi perubahan denylist.conf secara
// otomatis lewat inotify (companion root process memantau file, mendorong
// snapshot baru ke system_server lewat socket yang tetap terbuka — lihat
// native/src/main.cpp). Tidak ada cara murah untuk mengecek dari WebUI
// apakah watcher itu SEDANG hidup detik ini (perlu query lewat companion
// yang belum ada jalurnya), jadi baris ini menjelaskan FAKTA DESAIN modul,
// bukan hasil pengecekan real-time. Kalau watcher berhenti karena suatu
// sebab, tombol Action di Manager bisa dipakai untuk memicu ulang reload,
// dan hasilnya bisa dikonfirmasi lewat logcat (tag IsolPolicyZ).
// ---------------------------------------------------------------------------
function renderReloadStatus() {
  const dot = document.getElementById("reloadDot");
  const text = document.getElementById("reloadText");
  dot.className = "status-dot ok";
  text.textContent =
    "Perubahan di bawah aktif otomatis tanpa reboot (dipantau live). " +
    "Berlaku untuk aplikasi yang di-restart/dibuka setelah disimpan.";
}

async function renderSelinuxStatus() {
  const dot = document.getElementById("selinuxDot");
  const text = document.getElementById("selinuxText");
  const detail = document.getElementById("selinuxDetail");
  try {
    const enforce = (await runShell("getenforce 2>/dev/null")).trim();
    const context = (await runShell(
      "cat /proc/1/attr/current 2>/dev/null || echo unknown"
    )).trim();
    const moduleCtx = (await runShell(
      `ls -Zd /data/adm/modules/${MODULE_ID} 2>/dev/null | awk '{print $1}' || echo unknown`
    )).trim();

    if (enforce === "Enforcing") {
      dot.className = "status-dot ok";
      text.textContent = "SELinux: Enforcing (normal)";
    } else if (enforce === "Permissive") {
      dot.className = "status-dot warn";
      text.textContent = "SELinux: Permissive (bukan konfigurasi produksi normal)";
    } else {
      dot.className = "status-dot bad";
      text.textContent = "SELinux: status tidak terbaca (" + (enforce || "kosong") + ")";
    }

    detail.textContent =
      `getenforce         : ${enforce || "(tidak terbaca)"}\n` +
      `init context (pid1) : ${context || "(tidak terbaca)"}\n` +
      `module dir context   : ${moduleCtx || "(tidak terbaca)"}\n\n` +
      `Modul ini tidak mengubah sepolicy apapun — semua akses file\n` +
      `(denylist.conf) dilakukan lewat companion process root Zygisk,\n` +
      `bukan langsung dari system_server, supaya tetap patuh domain\n` +
      `SELinux system_server tanpa perlu policy tambahan.`;
  } catch (e) {
    dot.className = "status-dot bad";
    text.textContent = "SELinux: gagal cek (" + e.message + ")";
    detail.textContent = "Gagal mengambil info: " + e.message;
  }
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
  renderReloadStatus();
  renderSelinuxStatus(); // async, tidak perlu ditunggu, render sendiri saat selesai
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
    status.textContent =
      "Tersimpan & aktif otomatis (tanpa reboot). Berlaku saat aplikasi " +
      "dibuka/di-restart berikutnya — bukan untuk proses yang sudah berjalan.";
  } catch (e) {
    status.textContent = "Gagal menyimpan: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

init();
