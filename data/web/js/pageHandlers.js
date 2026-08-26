/** Shared save helper: POST JSON and surface the device's own message. */
async function postConfig(url, payload) {
  try {
    const res = await apiFetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await res.json().catch(() => ({}));
    return {
      ok: res.ok && data.status !== "error",
      message: data.message || (res.ok ? "Saved" : "Failed"),
    };
  } catch (e) {
    return { ok: false, message: "Request failed: " + e };
  }
}

async function getConfig(url) {
  const res = await apiFetch(url);
  if (!res.ok) throw new Error("HTTP " + res.status);
  return res.json();
}

function networkPage() {
  return {
    city: "",
    ssid: "",
    password: "",
    showPassword: false,
    networks: [],
    scanning: false,
    cityStatus: "",
    wifiStatus: "",
    connected: false,
    ip: "",

    async init() {
      try {
        const cfg = await getConfig("/api/v1/weather/config");
        this.city = cfg.city || "";
      } catch (e) {
        /* leave blank */
      }
      await this.refreshStatus();
    },

    async refreshStatus() {
      try {
        const s = await getConfig("/api/v1/wifi/status");
        this.connected = !!s.connected;
        this.ip = s.ip || "";
        if (s.ssid) this.ssid = this.ssid || s.ssid;
      } catch (e) {
        /* ignore */
      }
    },

    async saveCity() {
      const r = await postConfig("/api/v1/weather/config", { city: this.city });
      this.cityStatus = r.message;
    },

    /**
     * The device scans asynchronously, so poll until it reports "done".
     * A blocking scan would drop our connection when we are talking to the
     * device over its own access point.
     */
    async scan() {
      this.scanning = true;
      this.networks = [];
      this.wifiStatus = "Scanning…";

      const deadline = Date.now() + 20000;

      try {
        while (Date.now() < deadline) {
          const res = await apiFetch("/api/v1/wifi/scan");
          const data = await res.json();

          // Tolerate the older bare-array response too.
          if (Array.isArray(data)) {
            this.networks = data;
            break;
          }

          if (data.status === "done") {
            this.networks = data.networks || [];
            break;
          }

          if (data.status === "error") {
            this.wifiStatus = "Scan unavailable";
            this.scanning = false;
            return;
          }

          await new Promise((r) => setTimeout(r, 800));
        }

        // A real scan commonly reports the same SSID more than once (mesh
        // and dual-band routers broadcast one name from several radios/
        // channels), and Alpine's x-for keys on SSID — duplicates collide and
        // silently render nothing at all, not just extra rows. Keep the
        // strongest signal per name and drop anything with no name at all
        // (an empty SSID is unusable to "Use" anyway).
        const bestByName = new Map();
        for (const net of this.networks) {
          if (!net.ssid) continue;
          const prev = bestByName.get(net.ssid);
          if (!prev || (net.rssi || -999) > (prev.rssi || -999)) {
            bestByName.set(net.ssid, net);
          }
        }
        this.networks = [...bestByName.values()].sort(
          (a, b) => (b.rssi || 0) - (a.rssi || 0),
        );

        this.wifiStatus = this.networks.length
          ? `Found ${this.networks.length} network(s)`
          : "No networks found — try again";
      } catch (e) {
        this.wifiStatus = "Scan failed: " + e;
      }

      this.scanning = false;
    },

    use(net) {
      this.ssid = net.ssid;
    },

    async connect() {
      this.wifiStatus = "Connecting…";
      const r = await postConfig("/api/v1/wifi/connect", {
        ssid: this.ssid,
        password: this.password,
      });
      this.wifiStatus = r.message;
      await this.refreshStatus();
    },
  };
}

function weatherPage() {
  return {
    cfg: {
      city: "",
      interval_min: 20,
      wind: "ms",
      temp: "c",
      pressure: "hpa",
    },
    apiKey: "",
    forecastKey: "",
    apiKeySet: false,
    forecastKeySet: false,
    current: null,
    cityStatus: "",
    unitStatus: "",
    intervalStatus: "",
    keyStatus: "",
    forecastKeyStatus: "",

    async init() {
      try {
        const cfg = await getConfig("/api/v1/weather/config");
        this.cfg = {
          city: cfg.city || "",
          interval_min: cfg.interval_min || 20,
          wind: cfg.wind || "ms",
          temp: cfg.temp || "c",
          pressure: cfg.pressure || "hpa",
        };
        this.apiKeySet = !!cfg.api_key_set;
        this.forecastKeySet = !!cfg.forecast_key_set;
      } catch (e) {
        /* defaults */
      }
      await this.refreshCurrent();
    },

    async refreshCurrent() {
      try {
        this.current = await getConfig("/api/v1/weather/current");
      } catch (e) {
        this.current = null;
      }
    },

    async saveCity() {
      const r = await postConfig("/api/v1/weather/config", {
        city: this.cfg.city,
      });
      this.cityStatus = r.message;
      await this.refreshCurrent();
    },

    async saveUnits() {
      const r = await postConfig("/api/v1/weather/config", {
        wind: this.cfg.wind,
        temp: this.cfg.temp,
        pressure: this.cfg.pressure,
      });
      this.unitStatus = r.message;
      await this.refreshCurrent();
    },

    async saveInterval() {
      const r = await postConfig("/api/v1/weather/config", {
        interval_min: Number(this.cfg.interval_min),
      });
      this.intervalStatus = r.message;
    },

    async saveKey() {
      const r = await postConfig("/api/v1/weather/config", {
        api_key: this.apiKey,
      });
      this.keyStatus = r.message;
      this.apiKeySet = this.apiKey.length > 0;
      this.apiKey = "";
      await this.refreshCurrent();
    },

    async saveForecastKey() {
      const r = await postConfig("/api/v1/weather/config", {
        forecast_key: this.forecastKey,
      });
      this.forecastKeyStatus = r.message;
      this.forecastKeySet = this.forecastKey.length > 0;
      this.forecastKey = "";
    },
  };
}

function timePage() {
  return {
    cfg: {
      tz_mode: "auto",
      utc_offset_min: 0,
      hour_color: "#FFFFFF",
      minute_color: "#FFA500",
      second_color: "#FFFFFF",
      format12h: false,
      date_format: "DD/MM/YYYY",
      colon_blink: false,
      font: 0,
      ntp_server: "",
    },
    offsetSource: "manual",
    effectiveOffset: 0,
    tzLookupStatus: "",
    tzStatus: "",
    colorStatus: "",
    formatStatus: "",
    dateStatus: "",
    blinkStatus: "",
    fontStatus: "",
    ntpStatus: "",

    // Offsets are static markup in time.html: a <template x-for> inside a
    // <select> is invalid HTML and gets hoisted out by the parser.
    init() {
      return this.load();
    },

    async load() {
      try {
        const cfg = await getConfig("/api/v1/time/config");
        Object.assign(this.cfg, cfg);
        this.offsetSource = cfg.offset_source || "manual";
        this.effectiveOffset = cfg.effective_offset_min || 0;
        this.tzLookupStatus = cfg.tz_status || "";
      } catch (e) {
        /* defaults */
      }
    },

    // The offset select speaks minutes; ±HH:MM keeps the 45-minute zones
    // honest, where dividing by 60 renders UTC+05:45 as "+5.8 h".
    offsetLabel() {
      const total = Number(this.effectiveOffset) || 0;
      const abs = Math.abs(total);
      const hours = String(Math.floor(abs / 60)).padStart(2, "0");
      const minutes = String(abs % 60).padStart(2, "0");
      return (total < 0 ? "-" : "+") + hours + ":" + minutes;
    },

    sourceNote() {
      switch (this.offsetSource) {
        case "weather":
          return "from your city, via OpenWeatherMap";
        case "ip":
          return "from a lookup of this device's public IP";
        case "cached":
          return "the last offset resolved automatically";
        default:
          return this.cfg.tz_mode === "auto"
            ? "the manual offset, until an automatic lookup succeeds"
            : "the manual offset";
      }
    },

    save(fields, key) {
      return postConfig("/api/v1/time/config", fields).then((r) => {
        this[key] = r.message;
        return this.load();
      });
    },

    saveTz() {
      return this.save(
        {
          tz_mode: this.cfg.tz_mode,
          utc_offset_min: Number(this.cfg.utc_offset_min),
        },
        "tzStatus",
      );
    },
    saveColors() {
      return this.save(
        {
          hour_color: this.cfg.hour_color,
          minute_color: this.cfg.minute_color,
          second_color: this.cfg.second_color,
        },
        "colorStatus",
      );
    },
    saveFormat() {
      return this.save({ format12h: !!this.cfg.format12h }, "formatStatus");
    },
    saveDate() {
      return this.save({ date_format: this.cfg.date_format }, "dateStatus");
    },
    saveBlink() {
      return this.save({ colon_blink: !!this.cfg.colon_blink }, "blinkStatus");
    },
    saveFont() {
      return this.save({ font: Number(this.cfg.font) }, "fontStatus");
    },
    saveNtp() {
      return this.save({ ntp_server: this.cfg.ntp_server }, "ntpStatus");
    },
  };
}

function picturesPage() {
  return {
    cfg: { auto_display: true, shuffle: false, interval_s: 5 },
    status: "",

    async init() {
      try {
        const cfg = await getConfig("/api/v1/pictures/config");
        this.cfg.auto_display = !!cfg.auto_display;
        this.cfg.shuffle = !!cfg.shuffle;
        this.cfg.interval_s = cfg.interval_s || 5;
      } catch (e) {
        /* defaults */
      }
    },

    async save() {
      const r = await postConfig("/api/v1/pictures/config", {
        auto_display: !!this.cfg.auto_display,
        shuffle: !!this.cfg.shuffle,
        interval_s: Number(this.cfg.interval_s),
      });
      this.status = r.message;
    },
  };
}

function settingsPage() {
  return {
    cfg: {
      theme: 0,
      auto_switch: false,
      auto_switch_interval_s: 10,
      auto_switch_mask: 0,
      brightness: 60,
      night_mode: false,
      night_start: "22:00",
      night_end: "07:00",
      night_brightness: 20,
      rotation: 0,
    },
    themes: [],
    rotate: [],
    info: {},
    web: {
      auth_enabled: false,
      user: "admin",
      password: "",
      password_set: false,
      lifetime_s: 0,
      ap_mode: false,
      lifetime_remaining_s: 0,
    },
    themeStatus: "",
    switchStatus: "",
    brightnessStatus: "",
    nightStatus: "",
    rotationStatus: "",
    webStatus: "",
    resetStatus: "",
    logs: [],

    async init() {
      await this.load();
      await this.loadWeb();
      await this.loadInfo();
    },

    async load() {
      try {
        const cfg = await getConfig("/api/v1/display/config");
        Object.assign(this.cfg, cfg);
        this.themes = cfg.themes || [];
        this.rotate = this.themes.map(
          (_, i) => (Number(cfg.auto_switch_mask) & (1 << i)) !== 0,
        );
      } catch (e) {
        /* defaults */
      }
    },

    async loadWeb() {
      try {
        const web = await getConfig("/api/v1/web/config");
        Object.assign(this.web, web, { password: "" });
      } catch (e) {
        /* defaults */
      }
    },

    async loadInfo() {
      try {
        this.info = await getConfig("/api/v1/system/info");
      } catch (e) {
        this.info = {};
      }
    },

    async pickTheme(index) {
      this.cfg.theme = index;
      const r = await postConfig("/api/v1/display/config", { theme: index });
      this.themeStatus = r.message;
    },

    async saveSwitch() {
      const mask = this.rotate.reduce((m, on, i) => (on ? m | (1 << i) : m), 0);
      const r = await postConfig("/api/v1/display/config", {
        auto_switch: !!this.cfg.auto_switch,
        auto_switch_interval_s: Number(this.cfg.auto_switch_interval_s),
        auto_switch_mask: mask,
      });
      this.switchStatus = r.message;
    },

    async saveBrightness() {
      const r = await postConfig("/api/v1/display/config", {
        brightness: Number(this.cfg.brightness),
      });
      this.brightnessStatus = r.message;
    },

    async saveNight() {
      const r = await postConfig("/api/v1/display/config", {
        night_mode: !!this.cfg.night_mode,
        night_start: this.cfg.night_start,
        night_end: this.cfg.night_end,
        night_brightness: Number(this.cfg.night_brightness),
      });
      this.nightStatus = r.message;
    },

    async saveRotation() {
      const r = await postConfig("/api/v1/display/config", {
        rotation: Number(this.cfg.rotation),
      });
      this.rotationStatus = r.message;
    },

    async saveWeb() {
      const payload = {
        user: this.web.user,
        lifetime_s: Number(this.web.lifetime_s),
        auth_enabled: !!this.web.auth_enabled,
      };
      if (this.web.password) payload.password = this.web.password;

      const r = await postConfig("/api/v1/web/config", payload);
      this.webStatus = r.message;
      this.web.password = "";
      await this.loadWeb();
    },

    async factoryReset() {
      if (
        !confirm(
          "Clear all settings? Uploaded pictures are kept. The device will reboot.",
        )
      )
        return;
      const r = await postConfig("/api/v1/system/factory-reset", {});
      this.resetStatus = r.message;
    },

    async reboot() {
      if (!confirm("Reboot the device?")) return;
      await postConfig("/api/v1/reboot", {});
      this.resetStatus = "Rebooting…";
    },

    async fetchLogs() {
      try {
        const data = await getConfig("/api/v1/logs");
        this.logs = data.logs || [];
      } catch (e) {
        this.logs = ["Could not read logs: " + e];
      }
    },
  };
}
