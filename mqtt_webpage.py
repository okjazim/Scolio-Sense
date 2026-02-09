import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
 
import paho.mqtt.client as mqtt  # MQTT library
 
 
# --- 1. MQTT broker settings --------------------------------------
 
BROKER = "broker.emqx.io"   # Public test broker [web:13]
PORT = 1883                 # Default MQTT port (no TLS)
TOPIC = "demo/simple/topic" # Topic
 
 
# This variable will store the last message from MQTT
last_message = "No message yet"
 
 
# --- 2. MQTT callback functions -----------------------------------
 
def on_connect(client, userdata, flags, rc):
    print("Connected to broker with result code:", rc)
    client.subscribe(TOPIC)
    print(f"Subscribed to topic: {TOPIC}")
 
 
def on_message(client, userdata, msg):
    global last_message
    text = msg.payload.decode("utf-8")  # bytes -> string
    last_message = text                 # store for web page
    print(f"[MESSAGE] Topic: {msg.topic} | Payload: {text}")
 
 
# --- 3. Simple HTTP server handler --------------------------------
 
class SimpleHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Build a very simple HTML page
        html = f"""
        <html>
        <head><title>MQTT Demo</title></head>
        <body>
            <h1>MQTT latest message</h1>
            <p><b>Topic:</b> {TOPIC}</p>
            <p><b>Last message:</b> {last_message}</p>
        </body>
        </html>
        """
 
        # Send HTTP response
        self.send_response(200)
        self.send_header("Content-type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(html.encode("utf-8"))
 
 
def start_http_server(port=8000):
    server_address = ("", port)
    httpd = HTTPServer(server_address, SimpleHandler)
    print(f"HTTP server running on http://localhost:{port}")
    httpd.serve_forever()
 
 
# --- 4. Main program ----------------------------------------------
 
def main():
    # 4.1 Set up MQTT client
    client = mqtt.Client(client_id="python-simple-client")
    client.on_connect = on_connect
    client.on_message = on_message
 
    print("Connecting to broker:", BROKER)
    client.connect(BROKER, PORT)
    client.loop_start()  # background thread for MQTT
 
    # 4.2 Start HTTP server in another thread
    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()
 
    # 4.3 Publish one test message
    time.sleep(1)  # wait for connection
    message_text = "Hello from Python MQTT!"
    print(f"Publishing message: {message_text} -> {TOPIC}")
    client.publish(TOPIC, message_text)
 
    # 4.4 Keep the program running so browser can refresh
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Stopping MQTT loop and disconnecting...")
        client.loop_stop()
        client.disconnect()
        print("Done.")
 
 
if __name__ == "__main__":
    main()