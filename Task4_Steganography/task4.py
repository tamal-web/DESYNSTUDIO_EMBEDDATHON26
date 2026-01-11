# task4_phase1_and_receive.py
# Phase 1: Signal the Reef and receive the structured payload
# Phase 2: Reassemble the transmitted data into an image file and validate integrity
# Prints the sent request JSON, prints the structured payload JSON, reconstructs the image, and prints validation info
# Requirements: pip install paho-mqtt pillow

import json
import time
import base64
import io
import paho.mqtt.client as mqtt
from PIL import Image

SSID = "Airtel_Shri Radhey"
PASSWORD = "BankeyBihari@152"
MQTT_SERVER = "broker.mqttdashboard.com"
MQTT_PORT = 1883

TEAM_ID = "tamaldesyn"
HIDDEN_MESSAGE = "REEFING KRILLS :( CORALS BLOOM <3"
CHALLENGE_CODE = "grgeg_window"  # broker will respond on this topic per Task 4 spec

TOPIC_REQUEST = "kelpsaute/steganography"
TOPIC_RESPONSE = CHALLENGE_CODE

# Build request payload
request_payload = {
    "request": HIDDEN_MESSAGE,
    "agent_id": TEAM_ID
}

# Flags/state
received = {"done": False}

def reconstruct_and_validate_image(payload_json):
    """
    Phase 2: Restore transmitted data, confirm shape/structure, ensure integrity.
    Expected payload format:
      {"data": "<base64_or_binary_string>", "type": "png", "width": 64, "height": 64}
    This function:
      - Decodes 'data'
      - Rebuilds an image in-memory
      - Validates dimensions and format
      - Writes image to disk as 'reconstructed_image.png'
      - Prints validation results as JSON
    """
    result = {
        "phase2": "start",
        "validated": False,
        "errors": [],
        "expected": {
            "type": payload_json.get("type"),
            "width": payload_json.get("width"),
            "height": payload_json.get("height")
        }
    }

    # Extract expected metadata
    expected_type = (payload_json.get("type") or "").lower()
    expected_w = payload_json.get("width")
    expected_h = payload_json.get("height")
    data_field = payload_json.get("data")

    if data_field is None:
        result["errors"].append("Missing 'data' field in payload")
        print(json.dumps(result))
        return

    # Attempt base64 decode first; if it fails, treat as raw bytes (utf-8) then encode back
    decoded_bytes = None
    try:
        decoded_bytes = base64.b64decode(data_field, validate=True)
    except Exception:
        # Not valid base64, try interpreting the string directly as binary content (rare)
        try:
            decoded_bytes = data_field.encode("utf-8")
        except Exception as e:
            result["errors"].append(f"Data extraction failed: {str(e)}")
            print(json.dumps(result))
            return

    # Try opening the image from bytes
    try:
        img = Image.open(io.BytesIO(decoded_bytes))
        img.load()  # Ensure full decode
    except Exception as e:
        result["errors"].append(f"Image decode failed: {str(e)}")
        print(json.dumps(result))
        return

    # Validate format (PNG expected per spec)
    actual_format = (img.format or "").lower()
    result["actual_format"] = actual_format

    if expected_type and actual_format and expected_type != actual_format:
        result["errors"].append(f"Format mismatch: expected {expected_type}, got {actual_format}")

    # Validate dimensions
    actual_w, actual_h = img.size
    result["actual_size"] = {"width": actual_w, "height": actual_h}

    if (isinstance(expected_w, int) and isinstance(expected_h, int)):
        if actual_w != expected_w or actual_h != expected_h:
            result["errors"].append(f"Size mismatch: expected {expected_w}x{expected_h}, got {actual_w}x{actual_h}")

    # Save reconstructed image to disk for evidence
    out_name = "reconstructed_image.png" if actual_format == "png" else "reconstructed_image_out.png"
    try:
        # Preserve original bytes if already PNG; otherwise re-save in PNG
        if actual_format == "png":
            with open(out_name, "wb") as f:
                f.write(decoded_bytes)
        else:
            img.save(out_name, format="PNG")
        result["output_file"] = out_name
    except Exception as e:
        result["errors"].append(f"Saving image failed: {str(e)}")

    # Final validation status
    result["validated"] = (len(result["errors"]) == 0)
    result["phase2"] = "complete"
    print(json.dumps(result))

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        # Subscribe to the structured payload topic first to avoid race conditions
        client.subscribe(TOPIC_RESPONSE, qos=1)
        # Publish the request
        client.publish(TOPIC_REQUEST, json.dumps(request_payload), qos=1)
        print(json.dumps(request_payload))
    else:
        print(json.dumps({"error": f"MQTT connect failed with rc={rc}"}))

def on_message(client, userdata, msg):
    # Expecting: {"data": "<image.png/base64>", "type": "png", "width": 64, "height": 64}
    try:
        payload_str = msg.payload.decode("utf-8")
        payload_json = json.loads(payload_str)

        # Save raw payload to file (evidence)
        with open("raw_payload.txt", "w") as f:
            f.write(payload_str)

        # Print structured payload
        print(json.dumps({
            "topic": msg.topic,
            "payload": payload_json
        }))

        # Phase 2: Reassemble and validate
        reconstruct_and_validate_image(payload_json)

        received["done"] = True
        # Graceful disconnect shortly after receiving and processing the payload
        time.sleep(0.5)
        client.disconnect()
    except Exception as e:
        print(json.dumps({"error": f"decode/parse failed: {str(e)}", "raw": msg.payload.decode('utf-8', errors='ignore')}))

client = mqtt.Client(client_id=f"shrimp_phase1_{int(time.time()*1000)}", clean_session=True)
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(MQTT_SERVER, MQTT_PORT, keepalive=10)
    client.loop_forever()
except Exception as e:
    print(json.dumps({"error": str(e)}))
