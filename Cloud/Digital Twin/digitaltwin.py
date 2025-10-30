#!/usr/bin/env python3
import os
import json
import signal
import ssl
import sys
import time
from datetime import datetime
from threading import Event

from flask import Flask, jsonify, request
import paho.mqtt.client as mqtt


# -----------------------------
# Config via environment vars
# -----------------------------
MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "8883"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "status/door")

# TLS/mTLS files (required)
MQTT_CA_CERT = os.getenv("MQTT_CA_CERT", "./ca.crt")
MQTT_CLIENT_CERT = os.getenv("MQTT_CLIENT_CERT", "./digitaltwin.crt")
MQTT_CLIENT_KEY = os.getenv("MQTT_CLIENT_KEY", "./digitaltwin.key")

# Optional: client ID
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "digitat-twin")

# Optional: set to "true" to allow connecting to brokers with mismatched names (NOT recommended)
TLS_INSECURE = os.getenv("TLS_INSECURE", "false").lower() == "true"

# MQTT publish QoS and retain
MQTT_QOS = int(os.getenv("MQTT_QOS", "1"))
MQTT_RETAIN = os.getenv("MQTT_RETAIN", "false").lower() == "true"

# HTTP
HTTP_HOST = os.getenv("HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(os.getenv("HTTP_PORT", "8000"))


# -----------------------------
# MQTT setup
# -----------------------------
mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID, protocol=mqtt.MQTTv311)
stop_event = Event()
connected = {"ok": False, "rc": None, "reason": ""}
current_state = {"value": "unknown", "updated_at": None}


def on_connect(client, userdata, flags, rc, properties=None):
    connected["ok"] = (rc == 0)
    connected["rc"] = rc
    connected["reason"] = mqtt.error_string(rc)
    if rc == 0:
        print(f"[MQTT] Connected to {MQTT_HOST}:{MQTT_PORT}")
    else:
        print(f"[MQTT] Connection failed: {mqtt.error_string(rc)}", file=sys.stderr)


def on_disconnect(client, userdata, rc, properties=None):
    connected["ok"] = False
    connected["rc"] = rc
    connected["reason"] = mqtt.error_string(rc)
    if rc != 0:
        print(f"[MQTT] Unexpected disconnect: {mqtt.error_string(rc)}", file=sys.stderr)
    else:
        print("[MQTT] Disconnected")


def configure_tls(client: mqtt.Client):
    if not (os.path.isfile(MQTT_CA_CERT) and os.path.isfile(MQTT_CLIENT_CERT) and os.path.isfile(MQTT_CLIENT_KEY)):
        print("[MQTT] Missing TLS files; check MQTT_CA_CERT, MQTT_CLIENT_CERT, MQTT_CLIENT_KEY.", file=sys.stderr)
        sys.exit(1)

    client.tls_set(
        ca_certs=MQTT_CA_CERT,
        certfile=MQTT_CLIENT_CERT,
        keyfile=MQTT_CLIENT_KEY,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS_CLIENT,
    )
    client.tls_insecure_set(TLS_INSECURE)


def mqtt_connect_with_retry():
    configure_tls(mqtt_client)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.loop_start()

    # Retry with simple backoff
    backoff = 1
    while not connected["ok"]:
        try:
            print(f"[MQTT] Connecting to {MQTT_HOST}:{MQTT_PORT} ...")
            mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
            # wait for on_connect to fire
            for _ in range(20):  # up to ~2s
                if connected["ok"]:
                    break
                time.sleep(0.1)
            if connected["ok"]:
                break
        except Exception as e:
            print(f"[MQTT] Connect error: {e}", file=sys.stderr)
        time.sleep(backoff)
        backoff = min(backoff * 2, 30)  # cap at 30s

    print("[MQTT] Ready.")


def publish_status(state: str):
    if not connected["ok"]:
        # Opportunistic reconnect (non-blocking publish will still queue while disconnected)
        try:
            mqtt_client.reconnect()
        except Exception as e:
            print(f"[MQTT] Reconnect attempt failed: {e}", file=sys.stderr)

    info = mqtt_client.publish(MQTT_TOPIC, state, qos=MQTT_QOS, retain=MQTT_RETAIN)
    return info.rc


# -----------------------------
# HTTP (Flask) app
# -----------------------------
app = Flask(__name__)


@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok",
        "mqtt_connected": connected["ok"],
        "mqtt_last_rc": connected["rc"],
        "mqtt_last_reason": connected["reason"],
        "topic": MQTT_TOPIC
    }), 200


@app.route("/api/lock", methods=["POST"])
def api_lock():
    state="lock"
    rc = publish_status(state)
    if rc == mqtt.MQTT_ERR_SUCCESS:
        current_state["value"] = state
        current_state["updated_at"] = datetime.utcnow().isoformat() + "Z"
        return jsonify({"ok": True, "published": {"topic": MQTT_TOPIC, "state": state}}), 200
    return jsonify({"ok": False, "error": mqtt.error_string(rc)}), 502


@app.route("/api/unlock", methods=["POST"])
def api_unlock():
    state="unlock"
    rc = publish_status(state)
    if rc == mqtt.MQTT_ERR_SUCCESS:
        current_state["value"] = state
        current_state["updated_at"] = datetime.utcnow().isoformat() + "Z"
        return jsonify({"ok": True, "published": {"topic": MQTT_TOPIC, "state": state}}), 200
    return jsonify({"ok": False, "error": mqtt.error_string(rc)}), 502

@app.route("/api/state", methods=["GET"])
def api_state():
    return jsonify({
        "ok": True,
        "state": current_state["value"],
        "updated_at": current_state["updated_at"],
        "mqtt_connected": connected["ok"],
        "topic": MQTT_TOPIC
    }), 200


def _shutdown(*_):
    print("\n[HTTP] Shutting down...")
    stop_event.set()
    try:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    finally:
        os._exit(0)

@app.route("/", methods=["GET"])
def ui():
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Digital Twin Control</title>
  <style>
    :root {{ --fg:#222; --bg:#fff; --muted:#666; --ok:#12a150; --bad:#b4002d; --btn:#0d6efd; }}
    * {{ box-sizing: border-box; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif; }}
    body {{ margin: 0; background: var(--bg); color: var(--fg); }}
    .wrap {{ max-width: 560px; margin: 40px auto; padding: 24px; border-radius: 16px; border: 1px solid #eee; }}
    h1 {{ margin: 0 0 8px; font-size: 1.4rem; }}
    .sub {{ margin: 0 0 20px; color: var(--muted); font-size: .95rem; }}
    .state {{ display:flex; align-items:center; gap:10px; margin: 16px 0 24px; }}
    .dot {{ width:10px; height:10px; border-radius:50%; background: #999; }}
    .dot.ok {{ background: var(--ok); }}
    .dot.bad {{ background: var(--bad); }}
    .badge {{ padding: 2px 8px; border-radius: 999px; background: #f3f4f6; font-size: .85rem; }}
    .row {{ display:flex; gap: 10px; flex-wrap: wrap; }}
    button {{ appearance: none; border: 0; padding: 10px 14px; border-radius: 10px; cursor: pointer; font-weight: 600; }}
    .primary {{ background: var(--btn); color: white; }}
    .ghost {{ background: #f3f4f6; }}
    .topic {{ margin-top: 16px; font-size: .9rem; color: var(--muted); }}
    .small {{ font-size: .85rem; color: var(--muted); }}
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Digital Twin</h1>

    <div class="state">
      <div id="mqttDot" class="dot"></div>
      <div>
        <div>Current state: <span id="state" class="badge">unknown</span></div>
        <div class="small">Last updated: <span id="updated">–</span></div>
      </div>
    </div>

    <div class="row">
      <button class="primary" id="toggleBtn">Toggle</button>
      <button class="ghost" id="lockBtn">Lock</button>
      <button class="ghost" id="unlockBtn">Unlock</button>
    </div>

    <div class="topic">Topic: <code>{MQTT_TOPIC}</code></div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);

    async function getState() {{
      const r = await fetch("/api/state");
      if (!r.ok) throw new Error("state fetch failed");
      return r.json();
    }}

    async function post(path) {{
      const r = await fetch(path, {{ method: "POST" }});
      if (!r.ok) throw new Error(path + " failed");
      return r.json();
    }}

    function render(s) {{
      $("state").textContent = s.state ?? "unknown";
      $("updated").textContent = s.updated_at ?? "–";
      $("mqttDot").className = "dot " + (s.mqtt_connected ? "ok" : "bad");
    }}

    async function refresh() {{
      try {{
        const s = await getState();
        render(s);
      }} catch (e) {{
        console.error(e);
      }}
    }}

    $("toggleBtn").onclick = async () => {{
      const s = await getState();
      const next = (s.state === "unlock") ? "lock" : "unlock";
      await post("/api/" + next);
      await refresh();
    }};

    $("lockBtn").onclick = async () => {{ await post("/api/lock"); await refresh(); }};
    $("unlockBtn").onclick = async () => {{ await post("/api/unlock"); await refresh(); }};

    // initial + periodic refresh
    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>"""
    return html, 200, {"Content-Type": "text/html; charset=utf-8"}

def main():
    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    mqtt_connect_with_retry()
    print(f"[HTTP] Listening on {HTTP_HOST}:{HTTP_PORT}")
    # Flask dev server is fine for a small internal bridge; for production, run behind gunicorn/uvicorn.
    app.run(host=HTTP_HOST, port=HTTP_PORT)


if __name__ == "__main__":
    main()
