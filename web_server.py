#!/usr/bin/env python3
"""
Submarine Vision Web Server
Receives video from Pi, runs ML detection, serves to web browser
"""

import cv2
import socket
import threading
import time
from flask import Flask, Response, render_template_string
import numpy as np

app = Flask(__name__)

# Global variables
latest_frame = None
frame_lock = threading.Lock()
detection_enabled = True
detected_objects_list = []
object_count = 0

# HTML Template
HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>🚢 Submarine Vision - Live</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #0c1929 0%, #1e3c72 50%, #2a5298 100%);
            color: white;
            min-height: 100vh;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 20px;
        }
        .header {
            text-align: center;
            padding: 25px;
            background: rgba(0,0,0,0.4);
            border-radius: 15px;
            margin-bottom: 25px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }
        .header h1 {
            font-size: 2.8em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
        }
        .header p {
            opacity: 0.8;
            font-size: 1.1em;
        }
        .stats {
            display: flex;
            justify-content: center;
            gap: 30px;
            margin-top: 20px;
            flex-wrap: wrap;
        }
        .stat {
            background: rgba(0,0,0,0.3);
            padding: 15px 25px;
            border-radius: 10px;
            text-align: center;
        }
        .stat-label {
            font-size: 0.85em;
            opacity: 0.7;
            margin-bottom: 5px;
        }
        .stat-value {
            font-size: 2em;
            font-weight: bold;
        }
        .fps { color: #00ff88; }
        .objects { color: #ff9f43; }
        .bandwidth { color: #54a0ff; }
        .video-container {
            background: rgba(0,0,0,0.4);
            border-radius: 15px;
            padding: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.5);
        }
        #video {
            width: 100%;
            max-width: 1280px;
            border-radius: 10px;
            display: block;
            margin: 0 auto;
        }
        .controls {
            display: flex;
            justify-content: center;
            gap: 15px;
            margin-top: 20px;
            flex-wrap: wrap;
        }
        .btn {
            padding: 12px 30px;
            font-size: 1em;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            transition: all 0.3s;
            font-weight: bold;
            text-transform: uppercase;
        }
        .btn-primary {
            background: linear-gradient(135deg, #00ff88 0%, #00cc6a 100%);
            color: #0c1929;
        }
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(0,255,136,0.4);
        }
        .btn-secondary {
            background: linear-gradient(135deg, #ff6b6b 0%, #ee5a52 100%);
            color: white;
        }
        .btn-secondary:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(255,107,107,0.4);
        }
        .objects-panel {
            margin-top: 20px;
            padding: 20px;
            background: rgba(0,0,0,0.3);
            border-radius: 10px;
        }
        .objects-panel h3 {
            margin-bottom: 15px;
            font-size: 1.3em;
        }
        .objects-list {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
        }
        .object-tag {
            background: linear-gradient(135deg, #ff9f43 0%, #ff8c1a 100%);
            color: #0c1929;
            padding: 8px 18px;
            border-radius: 25px;
            font-weight: bold;
            font-size: 0.95em;
            animation: fadeIn 0.3s ease-in;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: scale(0.8); }
            to { opacity: 1; transform: scale(1); }
        }
        .no-objects {
            opacity: 0.5;
            font-style: italic;
        }
        @media (max-width: 768px) {
            .header h1 { font-size: 2em; }
            .stats { gap: 15px; }
            .stat { padding: 10px 15px; }
            .stat-value { font-size: 1.5em; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🚢 Submarine Vision System</h1>
            <p>Live underwater surveillance with AI object detection</p>
            <div class="stats">
                <div class="stat">
                    <div class="stat-label">FPS</div>
                    <div class="stat-value fps" id="fps">--</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Objects</div>
                    <div class="stat-value objects" id="objects">--</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Bandwidth</div>
                    <div class="stat-value bandwidth" id="bandwidth">-- KB/s</div>
                </div>
            </div>
        </div>

        <div class="video-container">
            <img id="video" src="/video_feed" alt="Submarine Camera">

            <div class="controls">
                <button class="btn btn-primary" onclick="toggleDetection()">
                    🤖 Toggle Detection
                </button>
                <button class="btn btn-secondary" onclick="takeSnapshot()">
                    📸 Take Snapshot
                </button>
            </div>

            <div class="objects-panel">
                <h3>🎯 Detected Objects:</h3>
                <div class="objects-list" id="objectsList">
                    <span class="no-objects">Waiting for video stream...</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        let detectionEnabled = true;
        let frameCount = 0;
        let lastUpdateTime = Date.now();

        // Update stats every second
        setInterval(() => {
            fetch('/get_stats')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('fps').textContent = data.fps;
                    document.getElementById('objects').textContent = data.count;
                    document.getElementById('bandwidth').textContent =
                        data.bandwidth + ' KB/s';
                });
        }, 1000);

        // Update detected objects every 500ms
        setInterval(() => {
            fetch('/get_objects')
                .then(r => r.json())
                .then(data => {
                    const objectsDiv = document.getElementById('objectsList');
                    if (data.objects && data.objects.length > 0) {
                        objectsDiv.innerHTML = data.objects
                            .map(o => `<span class="object-tag">${o}</span>`)
                            .join('');
                    } else {
                        objectsDiv.innerHTML = '<span class="no-objects">No objects detected</span>';
                    }
                });
        }, 500);

        function toggleDetection() {
            fetch('/toggle_detection', {method: 'POST'})
                .then(() => {
                    detectionEnabled = !detectionEnabled;
                    alert('Detection ' + (detectionEnabled ? 'ENABLED' : 'DISABLED'));
                });
        }

        function takeSnapshot() {
            const video = document.getElementById('video');
            const canvas = document.createElement('canvas');
            canvas.width = video.naturalWidth || 640;
            canvas.height = video.naturalHeight || 480;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(video, 0, 0);

            const link = document.createElement('a');
            link.download = 'submarine_snapshot_' + Date.now() + '.jpg';
            link.href = canvas.toDataURL('image/jpeg', 0.9);
            link.click();
        }
    </script>
</body>
</html>
"""

def receive_frames():
    """Background thread: Receive frames from Pi"""
    global latest_frame

    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('0.0.0.0', 5001))
    server_sock.listen(1)

    print("[INFO] Waiting for Pi on port 5001...")

    while True:
        try:
            client_sock, addr = server_sock.accept()
            print(f"[INFO] Connected to Pi: {addr[0]}")

            while True:
                # Receive frame size
                size_data = client_sock.recv(4)
                if not size_data or len(size_data) < 4:
                    break

                size = int.from_bytes(size_data, byteorder='big')

                # Receive frame data
                frame_data = b''
                while len(frame_data) < size:
                    packet = client_sock.recv(min(8192, size - len(frame_data)))
                    if not packet:
                        break
                    frame_data += packet

                # Decode frame
                frame_array = np.frombuffer(frame_data, dtype=np.uint8)
                frame = cv2.imdecode(frame_array, cv2.IMREAD_COLOR)

                if frame is not None:
                    with frame_lock:
                        latest_frame = frame

        except Exception as e:
            print(f"[ERROR] {e}")
            time.sleep(1)

# Initialize object detector
detector = None
classes = []
try:
    print("[INFO] Loading ML models...")
    net = cv2.dnn.readNetFromTensorflow(
        'frozen_inference_graph.pb',
        'ssd_mobilenet_v2_coco.pbtxt'
    )
    net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
    net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
    detector = net

    with open('coco_classes.txt', 'r') as f:
        classes = [line.strip() for line in f.readlines()]
    print(f"[INFO] Loaded {len(classes)} object classes")
except Exception as e:
    print(f"[WARNING] ML models not found: {e}")
    print("[INFO] Running without object detection")

def detect_objects(frame):
    """Run object detection on frame"""
    global detected_objects_list, object_count

    if not detector or not detection_enabled:
        return frame

    try:
        blob = cv2.dnn.blobFromImage(frame, 1.0, (300, 300),
                                     [127.5, 127.5, 127.5], True, False)
        detector.setInput(blob)
        detections = detector.forward()

        height, width = frame.shape[:2]
        detected_objects_list = []
        object_count = 0

        for i in range(detections.shape[2]):
            confidence = detections[0, 0, i, 2]

            if confidence > 0.5:
                class_id = int(detections[0, 0, i, 1])
                box = detections[0, 0, i, 3:7] * np.array([width, height, width, height])
                x1, y1, x2, y2 = box.astype(int)

                # Ensure box is within frame
                x1 = max(0, min(x1, width - 1))
                y1 = max(0, min(y1, height - 1))
                x2 = max(0, min(x2, width - 1))
                y2 = max(0, min(y2, height - 1))

                class_name = classes[class_id] if class_id < len(classes) else f"Class{class_id}"
                label = f"{class_name}: {confidence:.2f}"
                detected_objects_list.append(label)
                object_count += 1

                # Draw bounding box
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)

                # Draw label
                label_size, _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
                cv2.rectangle(frame, (x1, y1 - label_size[1] - 10),
                            (x1 + label_size[0], y1), (0, 255, 0), -1)
                cv2.putText(frame, label, (x1, y1 - 5),
                          cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 2)

    except Exception as e:
        pass

    return frame

# Stats tracking
frame_times = []
bandwidth_samples = []

def generate_frames():
    """Generate MJPEG stream with detection"""
    global latest_frame, frame_times, bandwidth_samples

    while True:
        with frame_lock:
            if latest_frame is None:
                time.sleep(0.1)
                continue

            frame = latest_frame.copy()

        # Run detection
        frame = detect_objects(frame)

        # Track FPS
        now = time.time()
        frame_times.append(now)
        frame_times = [t for t in frame_times if now - t < 1.0]

        # Encode as JPEG
        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
        frame_bytes = buffer.tobytes()

        # Track bandwidth
        bandwidth_samples.append(len(frame_bytes))
        bandwidth_samples = bandwidth_samples[-30:]  # Keep last 30 frames

        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/')
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route('/video_feed')
def video_feed():
    return Response(generate_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/toggle_detection', methods=['POST'])
def toggle_detection():
    global detection_enabled
    detection_enabled = not detection_enabled
    return {'enabled': detection_enabled}

@app.route('/get_stats')
def get_stats():
    fps = len(frame_times) if len(frame_times) > 1 else 0
    bandwidth = sum(bandwidth_samples) / len(bandwidth_samples) / 1024 if bandwidth_samples else 0
    return {
        'fps': fps,
        'count': object_count,
        'bandwidth': int(bandwidth)
    }

@app.route('/get_objects')
def get_objects():
    return {
        'count': object_count,
        'objects': detected_objects_list[-10:]  # Last 10 objects
    }

if __name__ == '__main__':
    # Start frame receiver thread
    receiver_thread = threading.Thread(target=receive_frames, daemon=True)
    receiver_thread.start()

    print("\n" + "="*70)
    print("🚢 SUBMARINE VISION WEB SERVER")
    print("="*70)
    print(f"\n✓ ML Detection: {'ENABLED' if detector else 'DISABLED (models not found)'}")
    print(f"\n📡 Access the web interface at:")
    print(f"   → http://localhost:5000")
    print(f"   → http://<your-laptop-ip>:5000")
    print(f"\n📱 Forward port 5000 on your router for internet access!")
    print("="*70 + "\n")

    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
