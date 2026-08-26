/**
 * Shared media manager used by both the Pictures and Weather pages.
 *
 * Folder upload is strictly sequential: ESP8266WebServer can only handle one
 * multipart request at a time, and one-at-a-time also keeps device RAM flat.
 */
// LittleFS on the ESP8266 is built with LFS_NAME_MAX = 32 and refuses to
// create anything longer, so names are shortened here rather than letting the
// upload fail on the device.
const MAX_NAME_LEN = 31;

function shortenName(name) {
  if (name.length <= MAX_NAME_LEN) return name;

  const dot = name.lastIndexOf(".");
  const ext = dot > 0 ? name.slice(dot).toLowerCase() : "";
  const stem = dot > 0 ? name.slice(0, dot) : name;

  // Short deterministic tag so two long names that share a prefix cannot
  // collapse onto the same short name.
  let hash = 0;
  for (let i = 0; i < name.length; i++) {
    hash = (hash * 31 + name.charCodeAt(i)) >>> 0;
  }
  const tag = "_" + hash.toString(36).slice(0, 4);

  const room = MAX_NAME_LEN - ext.length - tag.length;
  return stem.slice(0, Math.max(1, room)) + tag + ext;
}

function fileManager(dir) {
  return {
    dir: dir,
    files: [],
    loaded: false,
    freeBytes: 0,
    totalBytes: 0,
    usedBytes: 0,
    uploading: false,
    cancelRequested: false,
    queue: [],
    status: "",
    humanFileSize,

    get freeHR() {
      return humanFileSize(this.freeBytes);
    },

    async init() {
      await this.refresh();
    },

    async refresh() {
      this.loaded = false;
      try {
        const res = await apiFetch(`/api/v1/files?dir=${this.dir}`);
        const data = await res.json();
        this.files = data.files || [];
        this.freeBytes = data.freeBytes || 0;
        this.totalBytes = data.totalBytes || 0;
        this.usedBytes = data.usedBytes || 0;
      } catch (e) {
        this.files = [];
        this.status = "Could not read file list: " + e;
      }

      // Drop selections for files that no longer exist (deleted elsewhere,
      // or just deleted by us) so a stale checkbox can't linger checked.
      const present = new Set(this.files.map((f) => f.name));
      for (const name of Object.keys(this.checked)) {
        if (!present.has(name)) delete this.checked[name];
      }

      this.loaded = true;
    },

    /** Accept both a plain multi-select and a directory picker. */
    onPick(event) {
      const picked = Array.from(event.target.files || []);
      const accepted = picked.filter((f) => /\.(jpe?g|gif)$/i.test(f.name));
      const skipped = picked.length - accepted.length;

      let renamed = 0;
      this.queue = accepted.map((f) => {
        const short = shortenName(f.name);
        if (short !== f.name) renamed++;
        return {
          file: f,
          name: short,
          original: f.name,
          size: f.size,
          state: short !== f.name ? "will rename" : "queued",
        };
      });

      this.status =
        `${accepted.length} file(s) ready` +
        (skipped > 0 ? ` — ${skipped} skipped (not JPG/GIF)` : "") +
        (renamed > 0 ? ` — ${renamed} renamed to fit the ${MAX_NAME_LEN}-char limit` : "");
    },

    async uploadAll() {
      if (this.queue.length === 0) {
        this.status = "Choose files or a folder first";
        return;
      }

      this.uploading = true;
      this.cancelRequested = false;

      let done = 0;
      let failed = 0;

      for (const item of this.queue) {
        if (this.cancelRequested) {
          item.state = "cancelled";
          continue;
        }

        // Stop early rather than filling the volume and failing mid-write.
        if (item.size > this.freeBytes) {
          item.state = "no space";
          failed++;
          this.status = "Out of space on the device — stopping";
          break;
        }

        item.state = "uploading";

        try {
          const form = new FormData();
          form.append("upload", item.file, item.name);

          const res = await apiFetch(`/api/v1/files?dir=${this.dir}`, {
            method: "POST",
            body: form,
          });
          const data = await res.json().catch(() => ({}));

          if (res.ok && data.status === "ok") {
            item.state = "done";
            done++;
            if (typeof data.freeBytes === "number") {
              this.freeBytes = data.freeBytes;
            }
          } else {
            item.state = data.message || "failed";
            failed++;
          }
        } catch (e) {
          item.state = "failed";
          failed++;
        }
      }

      this.uploading = false;
      this.status = `Uploaded ${done}, failed ${failed}`;
      await this.refresh();
    },

    cancel() {
      this.cancelRequested = true;
      this.status = "Cancelling after the current file…";
    },

    clearQueue() {
      this.queue = [];
      this.status = "";
    },

    // name -> bool. A plain reactive object, so a checkbox can bind straight
    // to checked[name] without hunting through an array on every render.
    checked: {},
    deletingBulk: false,

    get selectedNames() {
      return Object.keys(this.checked).filter((name) => this.checked[name]);
    },

    get allChecked() {
      return (
        this.files.length > 0 &&
        this.files.every((f) => this.checked[f.name])
      );
    },

    toggleAll(value) {
      for (const f of this.files) this.checked[f.name] = value;
    },

    /** Single delete, no confirmation — the caller decides when to ask. */
    async deleteOne(name) {
      const res = await apiFetch(`/api/v1/files?dir=${this.dir}`, {
        method: "DELETE",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name }),
      });
      const data = await res.json().catch(() => ({}));
      return { ok: res.ok, message: data.message };
    },

    async remove(name) {
      if (!confirm(`Delete ${name}? This cannot be undone.`)) return;

      try {
        const r = await this.deleteOne(name);
        this.status = r.message || (r.ok ? "Deleted" : "Delete failed");
      } catch (e) {
        this.status = "Delete failed: " + e;
      }

      delete this.checked[name];
      await this.refresh();
    },

    /**
     * Delete every checked file (or, with none checked, every file — the
     * "delete all" case). Sequential, same as upload: the device only wants
     * one request at a time, and it keeps the status line meaningful.
     */
    async removeSelected() {
      const names =
        this.selectedNames.length > 0
          ? this.selectedNames
          : this.files.map((f) => f.name);

      if (names.length === 0) {
        this.status = "Nothing to delete";
        return;
      }

      const label =
        this.selectedNames.length > 0
          ? `${names.length} selected picture(s)`
          : `all ${names.length} picture(s)`;
      if (!confirm(`Delete ${label}? This cannot be undone.`)) return;

      this.deletingBulk = true;
      let done = 0;
      let failed = 0;

      for (const name of names) {
        try {
          const r = await this.deleteOne(name);
          if (r.ok) done++;
          else failed++;
        } catch (e) {
          failed++;
        }
        this.status = `Deleting… ${done + failed}/${names.length}`;
      }

      this.checked = {};
      this.deletingBulk = false;
      this.status = `Deleted ${done}, failed ${failed}`;
      await this.refresh();
    },

    async select(name) {
      try {
        const res = await apiFetch(`/api/v1/files/set?dir=${this.dir}`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ name }),
        });
        const data = await res.json().catch(() => ({}));
        this.status = data.message || (res.ok ? "Selected" : "Failed");
        return res.ok;
      } catch (e) {
        this.status = "Failed: " + e;
        return false;
      }
    },
  };
}
