import re
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "web_ui.h"
content = path.read_text(encoding="utf-8")
m = re.search(r'R"HTMLPAGE\((.*)\)HTMLPAGE"', content, re.DOTALL)
if not m:
    raise SystemExit("HTMLPAGE block not found")

html = m.group(1)
orig = len(html)
lines = [ln.strip() for ln in html.split("\n")]
lines = [ln for ln in lines if ln]
html2 = "\n".join(lines)
repl = [
    ("Device is busy (upload/sync in progress). Retrying...", "Device busy uploading. Retrying..."),
    ("Cannot reach device API. Check Wi-Fi/AP connection and retry.", "Cannot reach device. Check Wi-Fi/AP."),
    ("Device config not loaded. Reload the page or reconnect to the ESP32 AP.", "Config not loaded. Reload or reconnect to AP."),
    ("Panel refresh failed. Device may be busy uploading data.", "Refresh failed. Device may be busy."),
    ("Could not verify admin role (timeout or network). Retry login.", "Admin role check timed out. Retry login."),
    ("Logs to SD on change (dB change threshold) or heartbeat, then bulk uploads", "Logs to SD on change/heartbeat, then bulk upload"),
    ("If scan finds no networks, enter SSID and password here.", "Enter SSID/password if scan finds nothing."),
]
for a, b in repl:
    html2 = html2.replace(a, b)

print(f"html {orig} -> {len(html2)} saved {orig - len(html2)}")
path.write_text(content[: m.start(1)] + html2 + content[m.end(1) :], encoding="utf-8", newline="\n")
