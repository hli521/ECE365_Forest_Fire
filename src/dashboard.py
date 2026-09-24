#!/usr/bin/env python3
"""
Local LoRa sensor dashboard.

Parses the normal human-readable lines the ESP32 already prints over USB
Serial (the same output you see in `pio device monitor` - "Packet N...",
the Grid-EYE grid, "PMSA003I: ..." and "DHT11: ...") and serves a
live-updating dashboard at http://localhost:8000 - viewable only on this
computer. No WiFi on the board needed, and no special JSON output required
from the firmware.

Setup:
    pip install pyserial

Run:
    python3 dashboard.py /dev/cu.usbserial-YYYY
    (Windows: python dashboard.py COM5)

Then open http://localhost:8000 in your browser.
"""

import re
import sys
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
except ImportError:
    print("Missing dependency. Run: pip install pyserial")
    sys.exit(1)

BAUD_RATE = 115200  # match your platformio.ini monitor_speed
HTTP_PORT = 8000

# Shared state between the serial-reading thread and the HTTP server.
state_lock = threading.Lock()
latest = {"hasData": False}
last_update_monotonic = None

# ---- Line formats printed by server.cpp (see the loop() function) ----
PACKET_RE = re.compile(r"^Packet (\d+), RSSI (-?\d+\.?\d*) dBm, SNR (-?\d+\.?\d*) dB$")
GRID_STATUS_RE = re.compile(r"^Grid-EYE: (valid|unavailable)$")
PM_VALID_RE = re.compile(
    r"^PMSA003I:\s*PM1=(\d+(?:\.\d+)?)\s*PM2\.5=(\d+(?:\.\d+)?)\s*PM10=(\d+(?:\.\d+)?)\s*ug/m3\s*$",
    re.IGNORECASE,
)
PM_INVALID_RE = re.compile(r"^PMSA003I:\s*unavailable\s*$", re.IGNORECASE)
DHT_VALID_RE = re.compile(r"^DHT11: (-?\d+\.?\d*) C, (-?\d+\.?\d*) % RH$")
DHT_INVALID_RE = re.compile(r"^DHT11: unavailable$")
FIRE_RE = re.compile(r"^Fire: (NORMAL|SURVEILLANCE|FIRE|FIRE >80C) \(reasons: (.*)\)$")


def _is_number(token: str) -> bool:
    try:
        float(token)
        return True
    except ValueError:
        return False


def _new_packet_ctx():
    return {
        "sequence": None, "rssi": None, "snr": None,
        "gridEyeValid": None, "gridRows": [],
        "fireLevel": None, "fireReasons": None,
    }


def _finalize(ctx):
    """Called once DHT11 line (always the last line of a packet) arrives."""
    global latest, last_update_monotonic
    if ctx["sequence"] is None:
        return  # incomplete packet (e.g. we started listening mid-stream)

    grid_flat = [v for row in ctx["gridRows"] for v in row]
    while len(grid_flat) < 64:
        grid_flat.append(None)

    data = {
        "hasData": True,
        "sequence": ctx["sequence"],
        "rssi": ctx["rssi"],
        "snr": ctx["snr"],
        "gridEyeValid": bool(ctx.get("gridEyeValid")),
        "gridEye": grid_flat[:64],
        "pmValid": bool(ctx.get("pmValid")),
        "pm1": ctx.get("pm1", 0),
        "pm25": ctx.get("pm25", 0),
        "pm10": ctx.get("pm10", 0),
        "dhtValid": bool(ctx.get("dhtValid")),
        "temperature": ctx.get("temperature", 0.0),
        "humidity": ctx.get("humidity", 0.0),
        # None when the server firmware predates fire detection.
        "fireLevel": ctx.get("fireLevel"),
        "fireReasons": ctx.get("fireReasons"),
    }
    with state_lock:
        latest = data
        last_update_monotonic = time.monotonic()


def _process_line(line: str, ctx: dict) -> dict:
    """Feed one line into the parser state machine; returns the (possibly
    new) context to keep passing in on the next call."""
    m = PACKET_RE.match(line)
    if m:
        ctx = _new_packet_ctx()
        ctx["sequence"] = int(m.group(1))
        ctx["rssi"] = float(m.group(2))
        ctx["snr"] = float(m.group(3))
        return ctx

    m = GRID_STATUS_RE.match(line)
    if m:
        ctx["gridEyeValid"] = (m.group(1) == "valid")
        ctx["gridRows"] = []
        return ctx

    # The 8 pixel rows: tab-separated, always 8 tokens, each "ERR" or a number.
    tokens = line.split("\t")
    if (ctx["gridEyeValid"] is not None and len(ctx["gridRows"]) < 8
            and len(tokens) == 8
            and all(t == "ERR" or _is_number(t) for t in tokens)):
        ctx["gridRows"].append([None if t == "ERR" else float(t) for t in tokens])
        return ctx

    m = PM_VALID_RE.match(line)
    if m:
        ctx["pmValid"] = True
        ctx["pm1"] = int(float(m.group(1)))
        ctx["pm25"] = int(float(m.group(2)))
        ctx["pm10"] = int(float(m.group(3)))
        return ctx
    if PM_INVALID_RE.match(line):
        ctx["pmValid"] = False
        return ctx
    if line.startswith("PMSA003I"):
        # Didn't match either pattern - print exactly what we got (with any
        # hidden/odd characters visible via repr) so the regex can be fixed.
        print(f"DEBUG: unrecognized PMSA003I line: {line!r}")
        return ctx

    m = FIRE_RE.match(line)
    if m:
        ctx["fireLevel"], ctx["fireReasons"] = m.group(1), m.group(2)
        return ctx

    m = DHT_VALID_RE.match(line)
    if m:
        ctx["dhtValid"] = True
        ctx["temperature"], ctx["humidity"] = float(m.group(1)), float(m.group(2))
        _finalize(ctx)
        return _new_packet_ctx()
    if DHT_INVALID_RE.match(line):
        ctx["dhtValid"] = False
        _finalize(ctx)
        return _new_packet_ctx()

    return ctx  # unrecognized line (blank line, boot message, DEBUG:, etc.) - ignore


def serial_reader(port: str):
    ctx = _new_packet_ctx()
    while True:
        try:
            with serial.Serial(port, BAUD_RATE, timeout=1) as ser:
                print(f"Connected to {port} at {BAUD_RATE} baud.")
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue
                    try:
                        line = raw.decode("utf-8", errors="ignore").strip()
                    except Exception:
                        continue
                    if not line:
                        continue
                    ctx = _process_line(line, ctx)
        except serial.SerialException as e:
            print(f"Serial error ({e}); retrying in 2s...")
            time.sleep(2)


DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LoRa Sensor Dashboard</title>
<style>
  :root { color-scheme: dark; }
  body { background:#0f1115; color:#e6e6e6; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; margin:0; padding:28px; }
  h1 { font-size:1.6rem; margin:0 0 6px; }
  #status { color:#8a8f98; font-size:1rem; margin-bottom:28px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(260px,1fr)); gap:20px; margin-bottom:24px; }
  .card { background:#1a1d24; border-radius:14px; padding:22px 26px; }
  .card h2 { font-size:0.95rem; text-transform:uppercase; letter-spacing:0.06em; color:#9aa0aa; margin:0 0 12px; }
  .card .value { font-size:3rem; font-weight:700; line-height:1.1; font-variant-numeric:tabular-nums; }
  .card .unit { font-size:1.15rem; color:#9aa0aa; margin-left:6px; font-weight:500; }
  .card .subline { font-size:1.05rem; color:#c2c6cf; margin-top:8px; }
  .unavailable { color:#555; font-style:italic; font-size:1.3rem; }
  #thermalCanvas { width:100%; max-width:440px; aspect-ratio:1/1; border-radius:10px; image-rendering:pixelated; }
  .offline { color:#e05656; }
  #fireBanner { margin-bottom:24px; border-left:8px solid #444; }
  #fireBanner .value { font-size:2.4rem; }
  #fireBanner.normal { border-left-color:#3fa76a; }
  #fireBanner.normal .value { color:#5fd38d; }
  #fireBanner.surveillance { border-left-color:#d9a21b; background:#2a2414; }
  #fireBanner.surveillance .value { color:#f2c14e; }
  #fireBanner.fire { border-left-color:#e05656; background:#3a1616; }
  #fireBanner.fire .value { color:#ff7a7a; }
</style>
</head>
<body>
  <h1>LoRa Sensor Dashboard (local)</h1>
  <div id="status">Waiting for first packet&hellip;</div>

  <div class="card" id="fireBanner">
    <h2>Fire status</h2>
    <div class="value" id="fireLevel">&mdash;</div>
    <div class="subline" id="fireDetail">Waiting for first packet</div>
  </div>

  <div class="grid">
    <div class="card">
      <h2>Packet</h2>
      <div class="value" id="seq">&mdash;</div>
      <div class="subline">RSSI <span id="rssi">&mdash;</span> dBm &middot; SNR <span id="snr">&mdash;</span> dB</div>
    </div>
    <div class="card">
      <h2>PMSA003I &middot; PM2.5</h2>
      <div id="pmReading">
        <div class="value"><span id="pm25">&mdash;</span><span class="unit">&micro;g/m&sup3;</span></div>
        <div class="subline">PM1 <span id="pm1">&mdash;</span> &middot; PM10 <span id="pm10">&mdash;</span> &micro;g/m&sup3;</div>
      </div>
      <div class="unavailable" id="pmUnavailable" style="display:none;">unavailable</div>
    </div>
    <div class="card">
      <h2>DHT11 &middot; Temperature</h2>
      <div id="tempReading">
        <div class="value"><span id="temp">&mdash;</span><span class="unit">&deg;C</span></div>
      </div>
      <div class="unavailable" id="tempUnavailable" style="display:none;">unavailable</div>
    </div>
    <div class="card">
      <h2>DHT11 &middot; Humidity</h2>
      <div id="humReading">
        <div class="value"><span id="hum">&mdash;</span><span class="unit">%</span></div>
      </div>
      <div class="unavailable" id="humUnavailable" style="display:none;">unavailable</div>
    </div>
  </div>

  <div class="card" style="max-width:440px;">
    <h2>Grid-EYE 8&times;8 Thermal</h2>
    <div id="gridBlock">
      <canvas id="thermalCanvas" width="8" height="8"></canvas>
    </div>
  </div>

<script>
const canvas = document.getElementById('thermalCanvas');
const ctx = canvas.getContext('2d');

function tempToColor(t, min, max) {
  const f = Math.max(0, Math.min(1, (t - min) / Math.max(0.001, (max - min))));
  const r = Math.round(255 * f);
  const b = Math.round(255 * (1 - f));
  const g = Math.round(80 * (1 - Math.abs(f - 0.5) * 2));
  return `rgb(${r},${g},${b})`;
}

function renderGrid(pixels) {
  const valid = pixels.filter(v => v !== null);
  const min = valid.length ? Math.min(...valid) : 20;
  const max = valid.length ? Math.max(...valid) : 30;
  for (let i = 0; i < 64; i++) {
    const x = i % 8, y = Math.floor(i / 8);
    ctx.fillStyle = (pixels[i] === null) ? '#333' : tempToColor(pixels[i], min, max);
    ctx.fillRect(x, y, 1, 1);
  }
}

async function refresh() {
  try {
    const res = await fetch('/data', { cache: 'no-store' });
    if (!res.ok) throw new Error('bad response');
    const d = await res.json();

    document.getElementById('status').textContent =
      d.hasData ? `Last update: ${d.secondsAgo}s ago` : 'No packet received yet';
    document.getElementById('status').classList.toggle('offline', d.hasData && d.secondsAgo > 30);

    const banner = document.getElementById('fireBanner');
    banner.className = 'card';
    if (d.hasData && d.fireLevel) {
      const level = d.fireLevel;
      banner.classList.add(level.startsWith('FIRE') ? 'fire' :
                           level === 'SURVEILLANCE' ? 'surveillance' : 'normal');
      document.getElementById('fireLevel').textContent =
        level === 'NORMAL' ? 'No fire detected' :
        level === 'SURVEILLANCE' ? 'Surveillance: fire risk' :
        level === 'FIRE' ? 'FIRE DETECTED' : 'FIRE DETECTED (above 80 °C)';
      document.getElementById('fireDetail').textContent =
        `Level ${level} · thresholds crossed: ${d.fireReasons}`;
    } else {
      document.getElementById('fireLevel').textContent = '—';
      document.getElementById('fireDetail').textContent = d.hasData
        ? 'No fire status in packet (update device and server firmware)'
        : 'Waiting for first packet';
    }

    document.getElementById('seq').textContent = d.hasData ? d.sequence : '—';
    document.getElementById('rssi').textContent = d.hasData ? d.rssi.toFixed(1) : '—';
    document.getElementById('snr').textContent = d.hasData ? d.snr.toFixed(1) : '—';

    if (d.hasData && d.gridEyeValid) {
      document.getElementById('gridBlock').innerHTML = '';
      document.getElementById('gridBlock').appendChild(canvas);
      renderGrid(d.gridEye);
    } else {
      document.getElementById('gridBlock').innerHTML = '<div class="unavailable">unavailable</div>';
    }

    const pmOk = d.hasData && d.pmValid;
    document.getElementById('pmReading').style.display = pmOk ? '' : 'none';
    document.getElementById('pmUnavailable').style.display = pmOk ? 'none' : '';
    if (pmOk) {
      document.getElementById('pm25').textContent = d.pm25;
      document.getElementById('pm1').textContent = d.pm1;
      document.getElementById('pm10').textContent = d.pm10;
    }

    const dhtOk = d.hasData && d.dhtValid;
    document.getElementById('tempReading').style.display = dhtOk ? '' : 'none';
    document.getElementById('tempUnavailable').style.display = dhtOk ? 'none' : '';
    document.getElementById('humReading').style.display = dhtOk ? '' : 'none';
    document.getElementById('humUnavailable').style.display = dhtOk ? 'none' : '';
    if (dhtOk) {
      document.getElementById('temp').textContent = d.temperature.toFixed(1);
      document.getElementById('hum').textContent = d.humidity.toFixed(1);
    }
  } catch (e) {
    document.getElementById('status').textContent = 'Dashboard server not reachable';
    document.getElementById('status').classList.add('offline');
  }
}

refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # keep the console quiet; serial_reader already prints status

    def do_GET(self):
        if self.path == "/":
            body = DASHBOARD_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/data":
            with state_lock:
                data = dict(latest)
                if last_update_monotonic is not None:
                    data["secondsAgo"] = int(time.monotonic() - last_update_monotonic)
                else:
                    data["secondsAgo"] = 0
            body = json.dumps(data).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()


def main():
    if len(sys.argv) != 2:
        print(f"Usage: python3 {sys.argv[0]} <serial-port>")
        print("Example: python3 dashboard.py /dev/cu.usbserial-1420")
        sys.exit(1)

    port = sys.argv[1]
    reader_thread = threading.Thread(target=serial_reader, args=(port,), daemon=True)
    reader_thread.start()

    httpd = ThreadingHTTPServer(("localhost", HTTP_PORT), Handler)
    print(f"Dashboard running at http://localhost:{HTTP_PORT}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
