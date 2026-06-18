from flask import Flask, request, jsonify, render_template, send_from_directory
import sqlite3
import requests
import socket
import os
import threading

app = Flask(__name__)
DB_FILE = 'faces.db'
ESP32_IP = "192.168.4.1" # Fallback/initial IP

def make_safe_filename(name):
    return "".join([c if c.isalnum() or c in ('_', '-') else "_" for c in name])

@app.template_filter('safe_filename')
def safe_filename_filter(s):
    return make_safe_filename(s)

def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS faces (id INTEGER PRIMARY KEY, name TEXT)''')
    conn.commit()
    conn.close()

init_db()

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('10.255.255.255', 1))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

@app.route('/')
def index():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT * FROM faces')
    faces = c.fetchall()
    conn.close()
    local_ip = get_local_ip()
    return render_template('index.html', faces=faces, local_ip=local_ip, esp_ip=ESP32_IP)

@app.route('/webhook/sync', methods=['POST'])
def webhook_sync():
    data = request.json
    if data and 'id' in data and 'name' in data:
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('INSERT OR REPLACE INTO faces (id, name) VALUES (?, ?)', (data['id'], data['name']))
        conn.commit()
        conn.close()
        return jsonify({"status": "ok"}), 200
    return jsonify({"error": "Invalid data"}), 400

@app.route('/webhook/startup', methods=['POST'])
def webhook_startup():
    global ESP32_IP
    data = request.json
    if data and 'ip' in data:
        ESP32_IP = data['ip']
        print(f"[*] ESP32 registered IP via startup Webhook: {ESP32_IP}")
        return jsonify({"status": "ok"}), 200
    ESP32_IP = request.remote_addr
    print(f"[*] ESP32 registered IP via startup Webhook (remote_addr): {ESP32_IP}")
    return jsonify({"status": "ok"}), 200

@app.route('/change_esp_ip', methods=['POST'])
def change_esp_ip():
    global ESP32_IP
    data = request.json
    if data and 'ip' in data:
        ESP32_IP = data['ip']
        print(f"[*] ESP32 IP manually updated to: {ESP32_IP}")
        return jsonify({"status": "ok", "esp_ip": ESP32_IP}), 200
    return jsonify({"error": "Invalid IP"}), 400

logs_list = []
logs_lock = threading.Lock()

@app.route('/webhook/log', methods=['POST'])
def webhook_log():
    data = request.json
    if data and 'message' in data:
        log_msg = data['message']
        with logs_lock:
            logs_list.append(log_msg)
            if len(logs_list) > 50:
                logs_list.pop(0)
        print(f"[*] Received log from ESP32: {log_msg}")
        return jsonify({"status": "ok"}), 200
    return jsonify({"error": "Invalid data"}), 400

@app.route('/api/logs', methods=['GET'])
def api_get_logs():
    with logs_lock:
        return jsonify(logs_list), 200

@app.route('/webhook/upload_image', methods=['POST'])
def webhook_upload_image():
    face_id = request.args.get('id')
    if not face_id:
        return jsonify({"error": "Missing id parameter"}), 400
        
    try:
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('SELECT name FROM faces WHERE id = ?', (face_id,))
        row = c.fetchone()
        conn.close()
        
        name = row[0] if row else f"User_{face_id}"
        safe_name = make_safe_filename(name)
        
        os.makedirs('static/images', exist_ok=True)
        filepath = os.path.join('static/images', f"{safe_name}.jpg")
        
        with open(filepath, 'wb') as f:
            f.write(request.data)
            
        print(f"[*] Uploaded face image for ID {face_id} ({name}) -> {filepath}")
        return jsonify({"status": "ok"}), 200
    except Exception as e:
        print(f"[!] Image upload error: {e}")
        return jsonify({"error": str(e)}), 500

@app.route('/rename', methods=['POST'])
def rename():
    global ESP32_IP
    data = request.json
    if not data or 'id' not in data or 'name' not in data:
        return jsonify({"error": "Invalid data"}), 400
    face_id = data['id']
    new_name = data['name']
    
    # Fetch the old name from local SQLite to rename the image file
    old_name = None
    try:
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('SELECT name FROM faces WHERE id = ?', (face_id,))
        row = c.fetchone()
        if row:
            old_name = row[0]
        conn.close()
    except Exception as e:
        print(f"[!] Failed to fetch old name for image rename: {e}")
    
    # 1. Send rename request to ESP32
    try:
        r = requests.post(f'http://{ESP32_IP}/api/faces/rename', json={"id": face_id, "name": new_name}, timeout=5)
        if r.status_code != 200:
            return jsonify({"error": f"ESP32 returned error: {r.text}"}), r.status_code
    except Exception as e:
        return jsonify({"error": f"Failed to sync with ESP32: {str(e)}"}), 500
        
    # 2. Update local SQLite DB
    try:
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('UPDATE faces SET name = ? WHERE id = ?', (new_name, face_id))
        conn.commit()
        conn.close()
        
        # 3. Rename portrait image file if it exists
        if old_name:
            old_safe = make_safe_filename(old_name)
            new_safe = make_safe_filename(new_name)
            old_path = os.path.join('static/images', f"{old_safe}.jpg")
            new_path = os.path.join('static/images', f"{new_safe}.jpg")
            if os.path.exists(old_path):
                try:
                    os.makedirs('static/images', exist_ok=True)
                    os.rename(old_path, new_path)
                    print(f"[*] Renamed image file: {old_path} -> {new_path}")
                except Exception as e:
                    print(f"[!] Failed to rename image file from {old_path} to {new_path}: {e}")

        print(f"[*] Renamed ID {face_id} to '{new_name}' in database and synced to ESP32")
        return jsonify({"status": "ok"}), 200
    except Exception as e:
        return jsonify({"error": f"Local DB update failed: {str(e)}"}), 500

@app.route('/sync_from_esp', methods=['POST'])
def sync_from_esp():
    try:
        r = requests.get(f'http://{ESP32_IP}/api/faces', timeout=5)
        faces = r.json()
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('DELETE FROM faces')
        for f in faces:
            c.execute('INSERT INTO faces (id, name) VALUES (?, ?)', (f['id'], f['name']))
        conn.commit()
        conn.close()
        return jsonify({"status": "ok", "count": len(faces)})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/delete_all', methods=['POST'])
def delete_all():
    try:
        r = requests.delete(f'http://{ESP32_IP}/api/faces', timeout=5)
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('DELETE FROM faces')
        conn.commit()
        conn.close()
        
        # Clear all face image files
        if os.path.exists('static/images'):
            for filename in os.listdir('static/images'):
                filepath = os.path.join('static/images', filename)
                try:
                    if os.path.isfile(filepath):
                        os.remove(filepath)
                except Exception as e:
                    print(f"[!] Failed to delete image file {filepath}: {e}")
                    
        return jsonify({"status": "ok"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/firmware/<path:filename>')
def serve_firmware(filename):
    return send_from_directory('firmware', filename)

@app.route('/trigger_ota', methods=['POST'])
def trigger_ota():
    firmware_name = "FaceRecognitionS3.bin"
    url = f"http://{get_local_ip()}:8088/firmware/{firmware_name}"
    try:
        r = requests.post(f'http://{ESP32_IP}/api/ota_trigger', json={"url": url}, timeout=5)
        return jsonify(r.json())
    except Exception as e:
        return jsonify({"error": str(e)}), 500

def udp_discovery_listener():
    global ESP32_IP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(('0.0.0.0', 8090))
    except Exception as e:
        print(f"[!] UDP bind failed: {e}")
        return

    print("[*] UDP Discovery Listener started on port 8090...")
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode('utf-8', errors='ignore')
            if msg.startswith("ESP32_DISCOVER:"):
                parts = msg.split(":")
                esp_ip = parts[1] if len(parts) > 1 else addr[0]
                if not esp_ip or esp_ip == "0.0.0.0":
                    esp_ip = addr[0]
                ESP32_IP = esp_ip
                print(f"[*] Discovered ESP32 at: {ESP32_IP}")
                
                # Reply back to ESP32 with Mac's IP
                mac_ip = get_local_ip()
                reply_msg = f"SERVER_IP:{mac_ip}"
                sock.sendto(reply_msg.encode('utf-8'), (addr[0], 8090))
                print(f"[*] Sent response to ESP32: {reply_msg} -> {addr[0]}:8090")
        except Exception as e:
            print(f"[!] UDP thread error: {e}")

def udp_log_listener():
    import re
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(('0.0.0.0', 8091))
    except Exception as e:
        print(f"[!] UDP Log bind failed: {e}")
        return

    print("[*] UDP Log Listener started on port 8091...")
    while True:
        try:
            data, addr = sock.recvfrom(2048)
            msg = data.decode('utf-8', errors='ignore')
            # Strip ANSI escape codes
            clean_msg = ansi_escape.sub('', msg).strip()
            if clean_msg:
                lines = clean_msg.split('\n')
                with logs_lock:
                    for line in lines:
                        if line.strip():
                            logs_list.append(line.strip())
                    # Limit log history
                    while len(logs_list) > 100:
                        logs_list.pop(0)
        except Exception as e:
            print(f"[!] UDP Log thread error: {e}")

if __name__ == '__main__':
    init_db()
    
    # Start UDP discovery listener thread
    t = threading.Thread(target=udp_discovery_listener, daemon=True)
    t.start()
    
    # Start UDP log listener thread
    t_log = threading.Thread(target=udp_log_listener, daemon=True)
    t_log.start()
    
    print("=======================================")
    print("ESP32-S3 Face Recognition OTA Dashboard")
    print(f"Running on: http://{get_local_ip()}:8088")
    print("Connecting ESP32 and Mac automatically via UDP discovery...")
    print("=======================================")
    app.run(host='0.0.0.0', port=8088)
