#!/usr/bin/env python3
"""
Local sensor dashboard.

Reads the "JSON:{...}" lines printed by the ESP32 (see server.cpp) over the
same USB-serial connection you use for flashing/monitoring, and serves a
live-updating dashboard at http://localhost:8000 - viewable only on this
computer. No WiFi on the board needed.

Setup:
    pip install pyserial

Run:
    python3 dashboard.py /dev/cu.usbserial-YYYY
    (Windows: python dashboard.py COM5)

Then open http://localhost:8000 in your browser.
"""

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


def serial_reader(port: str):
    global latest, last_update_monotonic
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
                    if not line.startswith("JSON:"):
                        continue  # ignore the human-readable debug prints
                    payload = line[len("JSON:"):]
                    try:
                        data = json.loads(payload)
                    except json.JSONDecodeError:
                        continue
                    with state_lock:
                        latest = data
                        latest["hasData"] = True
                        last_update_monotonic = time.monotonic()
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
  body { background:#0f1115; color:#e6e6e6; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; margin:0; padding:20px; }
  h1 { font-size:1.3rem; margin:0 0 4px; }
  #status { color:#8a8f98; font-size:0.85rem; margin-bottom:20px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:16px; margin-bottom:20px; }
  .card { background:#1a1d24; border-radius:12px; padding:16px 18px; }
  .card h2 { font-size:0.8rem; text-transform:uppercase; letter-spacing:0.05em; color:#8a8f98; margin:0 0 8px; }
  .card .value { font-size:1.6rem; font-weight:600; }
  .card .unit { font-size:0.9rem; color:#8a8f98; margin-left:4px; }
  .unavailable { color:#555; font-style:italic; font-size:1.1rem; }
  #thermalCanvas { width:100%; max-width:400px; aspect-ratio:1/1; border-radius:8px; image-rendering:pixelated; }
  .offline { color:#e05656; }
</style>
</head>
<body>
  <h1>LoRa Sensor Dashboard (local)</h1>
  <div id="status">Waiting for first packet&hellip;</div>

  <div class="grid">
    <div class="card">
      <h2>Packet</h2>
      <div class="value" id="seq">&mdash;</div>
      <div class="unit">RSSI <span id="rssi">&mdash;</span> dBm &middot; SNR <span id="snr">&mdash;</span> dB</div>
    </div>
    <div class="card">
      <h2>PMSA003I</h2>
      <div id="pmBlock">
        <div class="value" id="pm25">&mdash;<span class="unit">&micro;g/m&sup3; PM2.5</span></div>
        <div class="unit">PM1 <span id="pm1">&mdash;</span> &middot; PM10 <span id="pm10">&mdash;</span> &micro;g/m&sup3;</div>
      </div>
    </div>
    <div class="card">
      <h2>DHT11</h2>
      <div id="dhtBlock">
        <div class="value" id="temp">&mdash;<span class="unit">&deg;C</span></div>
        <div class="unit">Humidity <span id="hum">&mdash;</span> %</div>
      </div>
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

    if (d.hasData && d.pmValid) {
      document.getElementById('pm25').innerHTML = d.pm25 + '<span class="unit">µg/m³ PM2.5</span>';
      document.getElementById('pm1').textContent = d.pm1;
      document.getElementById('pm10').textContent = d.pm10;
    } else {
      document.getElementById('pmBlock').innerHTML = '<div class="unavailable">unavailable</div>';
    }

    if (d.hasData && d.dhtValid) {
      document.getElementById('temp').innerHTML = d.temperature.toFixed(1) + '<span class="unit">°C</span>';
      document.getElementById('hum').textContent = d.humidity.toFixed(1);
    } else {
      document.getElementById('dhtBlock').innerHTML = '<div class="unavailable">unavailable</div>';
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
