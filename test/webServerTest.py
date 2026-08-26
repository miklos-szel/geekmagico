#!/usr/bin/env python3

"""
This spawns a webserver with API callbacks meant to emulate a device
Can be used to test the webpage separately from the ESP8266
This file should be ran from this local directory
"""

from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse
import base64
import json
import os
import threading
import time
import argparse

HOST = "localhost"
PORT = 8080
BASE_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "data/web")
)

class DeviceState:
    def __init__(self):
        self._data = {}
        self._lock = threading.Lock()

    def get(self, key, default=None):
        with self._lock:
            return self._data.get(key, default)

    def set(self, key, value):
        with self._lock:
            self._data[key] = value

    def update(self, mapping: dict):
        with self._lock:
            self._data.update(mapping)


class Router:
    def __init__(self):
        self._routes = {}

    def route(self, method: str, path: str):
        def decorator(func):
            self._routes[(method.upper(), path)] = func
            return func
        return decorator

    def dispatch(self, handler, method: str, path: str) -> bool:
        fn = self._routes.get((method.upper(), path))
        if not fn:
            return False
        fn(handler)
        return True


class APIHandler(SimpleHTTPRequestHandler):
    state: DeviceState = None
    router: Router = None
    base_path: str = BASE_PATH

    def __init__(self, *args, **kwargs):
        self.directory = self.base_path
        super().__init__(*args, **kwargs)

    def translate_path(self, path):
        orig = super().translate_path(path)
        rel = os.path.relpath(orig, os.getcwd())
        return os.path.join(self.base_path, rel)

    def json_response(self, payload=None, status=200):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        if payload is not None:
            self.wfile.write(json.dumps(payload).encode())

    def read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return {}
        try:
            return json.loads(self.rfile.read(length))
        except json.JSONDecodeError:
            return None

    def do_GET(self):
        time.sleep(self.state.get("d.responseDelay", 0))
        path = urlparse(self.path).path
        if self.router.dispatch(self, "GET", path):
            return
        super().do_GET()

    def do_POST(self):
        time.sleep(self.state.get("d.responseDelay", 0))
        path = urlparse(self.path).path
        if self.router.dispatch(self, "POST", path):
            return
        self.json_response({"error": "unknown route"}, 404)

    def do_DELETE(self):
        time.sleep(self.state.get("d.responseDelay", 0))
        path = urlparse(self.path).path
        if self.router.dispatch(self, "DELETE", path):
            return
        self.json_response({"error": "unknown route"}, 404)


router = Router()

def check_auth(h: APIHandler) -> bool:
    """Optional HTTP Basic auth, mirroring the device.

    Disabled by default, so every request passes. Set state key
    "web.auth_enabled" to require Basic credentials; a failure challenges the
    client the same way the firmware does.
    """
    if not h.state.get("web.auth_enabled", False):
        return True

    user = h.state.get("web.user", "admin")
    password = h.state.get("web.password", "")
    if not password:
        return True

    expected = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()

    if h.headers.get("Authorization", "") != expected:
        h.send_response(401)
        h.send_header("WWW-Authenticate", 'Basic realm="GeekMagicO"')
        h.send_header("Content-Type", "application/json")
        h.end_headers()
        h.wfile.write(b'{"status":"error","message":"Authentication required"}')
        return False

    return True

@router.route("GET", "/api/v1/wifi/status")
def wifi_status(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    h.json_response({
        "connected": h.state.get("wifi.connected"),
        "ssid": h.state.get("wifi.ssid"),
        "ip": h.state.get("wifi.ip"),
    })


@router.route("GET", "/api/v1/wifi/scan")
def wifi_scan(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    # Mirrors the firmware: the scan is async, so the first call reports
    # "scanning" and a later one returns the results.
    polls = h.state.get("wifi.scan_polls", 0)
    if polls < 1:
        h.state.set("wifi.scan_polls", polls + 1)
        return h.json_response({"status": "scanning", "networks": []})
    h.state.set("wifi.scan_polls", 0)
    nets = h.state.get("wifi.networks") or []
    h.json_response({"status": "done", "count": len(nets), "networks": nets})


@router.route("POST", "/api/v1/wifi/connect")
def wifi_connect(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    if data is None:
        return h.json_response({"error": "invalid json"}, 400)

    ssid = data.get("ssid", "")
    ip = "4.5.6.7"

    h.state.update({
        "wifi.connected": True,
        "wifi.ssid": ssid,
        "wifi.ip": ip,
    })

    time.sleep(h.state.get("d.wifiConnDelay", 0))
    h.json_response({"status": "connected", "ssid": ssid, "ip": ip})


@router.route("GET", "/api/v1/ntp/status")
def ntp_status(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    h.json_response({
        "lastOk": h.state.get("ntp.lastOk"),
        "lastStatus": h.state.get("ntp.lastStatus"),
        "lastSyncTime": h.state.get("ntp.lastSyncTime"),
    })


@router.route("GET", "/api/v1/ntp/config")
def ntp_config_get(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    h.json_response({"ntp_server": h.state.get("ntp.server")})


@router.route("POST", "/api/v1/ntp/config")
def ntp_config_set(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    if data is None:
        return h.json_response({"error": "invalid json"}, 400)

    server = data.get("ntp_server", "")
    h.state.set("ntp.server", server)
    h.json_response({"status": "ok", "ntp_server": server})


@router.route("POST", "/api/v1/ntp/sync")
def ntp_sync(h: APIHandler):
    if not check_auth(h):
        return
    h.state.update({
        "ntp.lastStatus": "syncing",
        "ntp.lastSyncTime": time.time(),
        "ntp.lastOk": True,
    })
    h.json_response({"status": "ok"})


@router.route("GET", "/api/v1/ota/status")
def ota_status(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    h.json_response({
        "inProgress": h.state.get("ota.inProgress"),
        "bytesWritten": h.state.get("ota.bytesWritten"),
        "totalBytes": h.state.get("ota.totalBytes"),
        "error": h.state.get("ota.error"),
        "message": h.state.get("ota.message"),
    })


@router.route("POST", "/api/v1/ota/fw")
@router.route("POST", "/api/v1/ota/fs")
def ota_start(h: APIHandler):
    if not check_auth(h):
        return
    total = int(h.headers.get("Content-Length", 0))
    h.state.update({
        "ota.inProgress": True,
        "ota.bytesWritten": 0,
        "ota.totalBytes": total,
        "ota.error": False,
        "ota.message": "upload started",
    })
    h.json_response({"status": "upload started"})


@router.route("POST", "/api/v1/ota/cancel")
def ota_cancel(h: APIHandler):
    if not check_auth(h):
        return
    h.state.update({
        "ota.inProgress": False,
        "ota.error": True,
        "ota.message": "cancelled",
    })
    h.json_response({"status": "cancelled"})


@router.route("GET", "/api/v1/gif")
def gif_list(h: APIHandler):
    if not check_auth(h):
        return
    time.sleep(h.state.get("d.getActionDelay", 0))
    h.json_response(h.state.get("gif.list"))


@router.route("POST", "/api/v1/gif")
def gif_upload(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    name = data.get("name", "uploaded.gif") if data else "uploaded.gif"
    h.json_response({"status": "success", "filename": name})


@router.route("DELETE", "/api/v1/gif")
def gif_delete(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    if not data or "name" not in data:
        return h.json_response({"error": "missing name"}, 400)
    h.json_response({"status": "success", "file": data["name"]})


@router.route("POST", "/api/v1/gif/play")
def gif_play(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    if not data:
        return h.json_response({"error": "invalid json"}, 400)
    h.state.set("gif.playing", data.get("name"))
    h.json_response({"status": "playing"})


@router.route("POST", "/api/v1/gif/stop")
def gif_stop(h: APIHandler):
    if not check_auth(h):
        return
    h.state.set("gif.playing", None)
    h.json_response({"status": "stopped"})


@router.route("POST", "/api/v1/reboot")
def reboot(h: APIHandler):
    if not check_auth(h):
        return
    h.json_response({"status": "rebooting"})


# ---------------------------------------------------------------- GeekMagicO
# Routes backing the five-tab UI. State lives in DeviceState so saves persist
# for the lifetime of the mock, which is what makes the UI testable.

def _merge(h: APIHandler, key: str, defaults: dict):
    """Merge a posted JSON body into a state dict and return it."""
    data = h.read_json()
    if data is None:
        return None
    cur = dict(state_defaults(h, key, defaults))
    for k, v in data.items():
        cur[k] = v
    h.state.set(key, cur)
    return cur


def state_defaults(h: APIHandler, key: str, defaults: dict) -> dict:
    cur = h.state.get(key)
    if cur is None:
        h.state.set(key, dict(defaults))
        return dict(defaults)
    return cur


WEATHER_DEFAULTS = {
    "city": "vienna", "interval_min": 20, "wind": "kmh", "temp": "c",
    "pressure": "hpa", "gif": "auto", "api_key": "", "forecast_key": "",
}
TIME_DEFAULTS = {
    "tz_mode": "auto", "utc_offset_min": 120, "auto_offset_min": 120, "hour_color": "#FFFFFF",
    "minute_color": "#FFFFFF", "second_color": "#FFFFFF", "format12h": False,
    "date_format": "YYYY-MM-DD", "colon_blink": False, "font": 0,
    "ntp_server": "",
}
PICTURE_DEFAULTS = {"auto_display": True, "shuffle": True, "interval_s": 30, "current": ""}
DISPLAY_DEFAULTS = {
    "theme": 0, "auto_switch": False, "auto_switch_interval_s": 30,
    "auto_switch_mask": 0, "brightness": 60, "night_mode": True,
    "night_start": "22:00", "night_end": "07:00", "night_brightness": 15,
    "rotation": 0,
}
WEB_DEFAULTS = {
    "auth_enabled": False, "user": "admin", "lifetime_s": 0,
}
THEMES = [
    "Weather Clock Today", "Weather Forecast", "Photo Album",
    "Time Style 1", "Time Style 2", "Time Style 3", "Simple Weather Clock",
]


@router.route("GET", "/api/v1/weather/config")
def weather_config_get(h: APIHandler):
    if not check_auth(h):
        return
    cfg = state_defaults(h, "weather", WEATHER_DEFAULTS)
    out = {k: v for k, v in cfg.items() if k not in ("api_key", "forecast_key")}
    out["api_key_set"] = bool(cfg.get("api_key"))
    out["forecast_key_set"] = bool(cfg.get("forecast_key"))
    h.json_response(out)


@router.route("POST", "/api/v1/weather/config")
def weather_config_set(h: APIHandler):
    if not check_auth(h):
        return
    if _merge(h, "weather", WEATHER_DEFAULTS) is None:
        return h.json_response({"status": "error", "message": "Invalid JSON"}, 400)
    h.json_response({"status": "ok", "message": "Weather settings saved"})


@router.route("GET", "/api/v1/weather/current")
def weather_current(h: APIHandler):
    if not check_auth(h):
        return
    cfg = state_defaults(h, "weather", WEATHER_DEFAULTS)
    h.json_response({
        "valid": True, "status": "ok",
        "provider": "openweathermap" if cfg.get("api_key") else "keyless",
        "temp": 19.0, "feels_like": 17.0, "wind": 2.8, "pressure": 1013,
        "humidity": 43, "condition": "Cloudy", "description": "overcast",
        "temp_unit": "C", "wind_unit": "km/h", "pressure_unit": "hPa",
    })


@router.route("POST", "/api/v1/weather/refresh")
def weather_refresh(h: APIHandler):
    if not check_auth(h):
        return
    h.json_response({"status": "ok", "message": "ok"})


@router.route("GET", "/api/v1/time/config")
def time_config_get(h: APIHandler):
    if not check_auth(h):
        return
    cfg = dict(state_defaults(h, "time", TIME_DEFAULTS))
    weather = state_defaults(h, "weather", WEATHER_DEFAULTS)
    if cfg.get("tz_mode") == "manual":
        cfg["offset_source"] = "manual"
    elif weather.get("api_key"):
        cfg["offset_source"] = "weather"
    else:
        cfg["offset_source"] = "ip"
    cfg["effective_offset_min"] = cfg.get("utc_offset_min", 0)
    cfg["tz_status"] = "ok"
    h.json_response(cfg)


@router.route("POST", "/api/v1/time/config")
def time_config_set(h: APIHandler):
    if not check_auth(h):
        return
    if _merge(h, "time", TIME_DEFAULTS) is None:
        return h.json_response({"status": "error", "message": "Invalid JSON"}, 400)
    h.json_response({"status": "ok", "message": "Time settings saved"})


@router.route("GET", "/api/v1/pictures/config")
def pictures_config_get(h: APIHandler):
    if not check_auth(h):
        return
    h.json_response(state_defaults(h, "pictures", PICTURE_DEFAULTS))


@router.route("POST", "/api/v1/pictures/config")
def pictures_config_set(h: APIHandler):
    if not check_auth(h):
        return
    if _merge(h, "pictures", PICTURE_DEFAULTS) is None:
        return h.json_response({"status": "error", "message": "Invalid JSON"}, 400)
    h.json_response({"status": "ok", "message": "Picture settings saved"})


@router.route("GET", "/api/v1/display/config")
def display_config_get(h: APIHandler):
    if not check_auth(h):
        return
    cfg = dict(state_defaults(h, "display", DISPLAY_DEFAULTS))
    cfg["themes"] = THEMES
    cfg["theme_name"] = THEMES[cfg.get("theme", 0)]
    cfg["night_active"] = False
    h.json_response(cfg)


@router.route("POST", "/api/v1/display/config")
def display_config_set(h: APIHandler):
    if not check_auth(h):
        return
    cur = _merge(h, "display", DISPLAY_DEFAULTS)
    if cur is None:
        return h.json_response({"status": "error", "message": "Invalid JSON"}, 400)
    if cur.get("theme", 0) >= len(THEMES):
        return h.json_response({"status": "error", "message": "theme out of range"}, 400)
    h.json_response({"status": "ok", "message": "Display settings saved"})


@router.route("GET", "/api/v1/web/config")
def web_config_get(h: APIHandler):
    if not check_auth(h):
        return
    cfg = dict(state_defaults(h, "web", WEB_DEFAULTS))
    cfg["password_set"] = bool(h.state.get("web.password", ""))
    cfg["lifetime_remaining_s"] = 0
    cfg["ap_mode"] = False
    h.json_response(cfg)


@router.route("POST", "/api/v1/web/config")
def web_config_set(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json()
    if data is None:
        return h.json_response({"status": "error", "message": "Invalid JSON"}, 400)

    if "password" in data:
        h.state.set("web.password", data.pop("password"))

    # Mirrors the firmware: refuse to arm auth with no password set.
    if data.get("auth_enabled") and not h.state.get("web.password", ""):
        return h.json_response(
            {"status": "error", "message": "Set a password before enabling auth"}, 400)

    cur = dict(state_defaults(h, "web", WEB_DEFAULTS))
    cur.update(data)
    h.state.set("web", cur)
    h.state.set("web.auth_enabled", cur.get("auth_enabled", False))
    h.state.set("web.user", cur.get("user", "admin"))
    h.json_response({"status": "ok", "message": "Web settings saved"})


@router.route("GET", "/api/v1/system/info")
def system_info(h: APIHandler):
    if not check_auth(h):
        return
    display = state_defaults(h, "display", DISPLAY_DEFAULTS)
    h.json_response({
        "model": "SmallTV-Ultra", "firmware": "GeekMagicO", "version": "dev",
        "free_heap": 27000, "chip_id": 123456, "uptime_s": 42,
        "fs_total": 2000000, "fs_used": 500000, "fs_free": 1500000,
        "theme": display.get("theme", 0),
        "theme_name": THEMES[display.get("theme", 0)],
        "ip": "192.168.4.1", "ap_mode": False, "ssid": "TestSSID",
    })


@router.route("POST", "/api/v1/system/factory-reset")
def system_factory_reset(h: APIHandler):
    if not check_auth(h):
        return
    h.json_response({"status": "ok", "message": "Settings cleared, rebooting"})


def _files_key(h: APIHandler) -> str:
    d = "gif" if "dir=gif" in (h.path or "") else "image"
    return f"files.{d}"


def _seed_gif_files() -> list:
    """The bundled wx-*.gif set, read from data/gif so the mock cannot drift."""
    gif_dir = os.path.normpath(os.path.join(BASE_PATH, "..", "gif"))
    bundled = []
    if os.path.isdir(gif_dir):
        for name in sorted(os.listdir(gif_dir)):
            if name.startswith("wx-") and name.endswith(".gif"):
                bundled.append(
                    {"name": name, "size": os.path.getsize(os.path.join(gif_dir, name))}
                )
    return bundled + [{"name": "spaceman.gif", "size": 61000}]


@router.route("GET", "/api/v1/files")
def files_list(h: APIHandler):
    if not check_auth(h):
        return
    key = _files_key(h)
    files = h.state.get(key)
    if files is None:
        files = [{"name": "sample.jpg", "size": 21000}] if key.endswith("image") \
            else _seed_gif_files()
        h.state.set(key, files)
    used = sum(f["size"] for f in files)
    h.json_response({
        "dir": "/" + key.split(".")[1], "files": files,
        "totalBytes": 2000000, "usedBytes": used,
        "freeBytes": 2000000 - used,
    })


@router.route("POST", "/api/v1/files")
def files_upload(h: APIHandler):
    if not check_auth(h):
        return
    # Record an entry so the UI's post-upload refresh shows something real.
    key = _files_key(h)
    files = list(h.state.get(key) or [])
    length = int(h.headers.get("Content-Length", 0) or 0)
    name = f"upload-{len(files) + 1}.jpg"
    h.rfile.read(length) if length else None
    files.append({"name": name, "size": max(1, length)})
    h.state.set(key, files)
    h.json_response({"status": "ok", "file": name, "freeBytes": 1000000})


@router.route("DELETE", "/api/v1/files")
def files_delete(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json() or {}
    key = _files_key(h)
    files = [f for f in (h.state.get(key) or []) if f["name"] != data.get("name")]
    h.state.set(key, files)
    if key.endswith("gif"):
        cfg = dict(state_defaults(h, "weather", WEATHER_DEFAULTS))
        # Only an exact filename match clears it, so "auto" survives losing an
        # individual icon -- same rule as FilesApi.cpp.
        if cfg.get("gif") == data.get("name"):
            cfg["gif"] = ""
            h.state.set("weather", cfg)
    h.json_response({"status": "ok", "message": "File removed"})


@router.route("POST", "/api/v1/files/set")
def files_set(h: APIHandler):
    if not check_auth(h):
        return
    data = h.read_json() or {}
    name = data.get("name", "")
    h.state.set("files.selected", name)
    if "dir=gif" in (h.path or ""):
        cfg = dict(state_defaults(h, "weather", WEATHER_DEFAULTS))
        cfg["gif"] = name
        h.state.set("weather", cfg)
    h.json_response({"status": "ok", "message": "Selection saved"})


def make_handler(state: DeviceState, router: Router):
    class BoundHandler(APIHandler):
        pass
    BoundHandler.state = state
    BoundHandler.router = router
    BoundHandler.base_path = BASE_PATH
    return BoundHandler


def run_server(httpd: HTTPServer):
    httpd.serve_forever(poll_interval=0.2)

def run_cli(httpd: HTTPServer, state: DeviceState):
    while True:
        try:
            cmd = input("").strip().split()
        except (EOFError, KeyboardInterrupt):
            cmd = ["quit"]

        if not cmd:
            continue

        if cmd[0] in ("q", "quit", "exit"):
            httpd.shutdown()
            httpd.server_close()
            print("Server stopped")
            return

        if cmd[0] == "set" and len(cmd) >= 3:
            state.set(cmd[1], cmd[2])
            print("OK")
        else:
            print("Commands: set <key> <value>, quit")

if __name__ == "__main__":
    state = DeviceState()

    state.set("d.wifiConnDelay", 1)
    state.set("d.responseDelay", 0)
    state.set("d.getActionDelay", 0.5)

    state.update({
        "wifi.ip": "1.2.3.4",
        "wifi.connected": True,
        "wifi.ssid": "TestSSID",
        "wifi.networks": [
            {"ssid": "ABC", "rssi": 0, "enc": 7},
            {"ssid": "Hi There!", "rssi": -50, "enc": 5},
        ],
    })

    state.set("gif.list", {
        "usedBytes": 1500,
        "totalBytes": 50000,
        "freeBytes": 48500,
        "files": [
            {"name": "test.gif", "size": 1000},
            {"name": "[BIG SHOT].gif", "size": 500},
        ],
    })

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--host', '-h', default=HOST, help='Host to bind (default: %(default)s)')
    parser.add_argument('--port', '-P', type=int, default=PORT, help='Port to bind (default: %(default)s)')
    parser.add_argument('--help', action='help', help='show this help message and exit')
    args = parser.parse_args()

    bind_host = args.host
    bind_port = args.port

    HandlerClass = make_handler(state, router)
    httpd = HTTPServer((bind_host, bind_port), HandlerClass)

    threading.Thread(target=run_server, args=(httpd,), daemon=True).start()
    print(f"HTTP listening on http://{bind_host}:{bind_port} (serving {BASE_PATH}/)")

    run_cli(httpd, state)
