#!/usr/bin/env python3
"""
3D Point Cloud Receiver for Submarine Vision System
Receives raw depth data via UDP and displays 3D point cloud
"""

import socket
import numpy as np
import cv2
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from matplotlib.animation import FuncAnimation
import threading
import sys

class DepthReceiver3D:
    def __init__(self, port=5000):
        self.port = port
        self.running = True
        self.depth_frame = None
        self.frame_lock = threading.Lock()

        # Create UDP socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', port))
        self.sock.settimeout(1.0)

        print(f"=== 3D Depth Receiver ===")
        print(f"Listening on port: {port}")
        print("Waiting for raw depth data...")

        # Start receiver thread
        self.recv_thread = threading.Thread(target=self.receive_loop, daemon=True)
        self.recv_thread.start()

    def receive_loop(self):
        """Receive raw depth frames from UDP"""
        buffer = b''

        while self.running:
            try:
                data, _ = self.sock.recvfrom(65536)

                if len(data) >= 12:
                    frame_id = int.from_bytes(data[0:4], 'little')
                    frame_type = int.from_bytes(data[4:8], 'little')
                    data_size = int.from_bytes(data[8:12], 'little')

                    # Only process raw depth (frame_type=3)
                    if frame_type == 3 and len(data) > 12:
                        buffer += data[12:]

                        if len(buffer) >= data_size:
                            png_data = buffer[:data_size]
                            buffer = buffer[data_size:]

                            # Decode PNG
                            depth_img = cv2.imdecode(np.frombuffer(png_data, dtype=np.uint8), cv2.IMREAD_UNCHANGED)

                            if depth_img is not None:
                                with self.frame_lock:
                                    self.depth_frame = depth_img

            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"Receive error: {e}")
                break

    def create_point_cloud(self, depth_img):
        """Create 3D point cloud from depth image"""
        # Downsample for performance
        step = 4

        # Get depth values
        h, w = depth_img.shape

        # Create coordinate grid
        x = np.arange(0, w, step)
        y = np.arange(0, h, step)
        xx, yy = np.meshgrid(x, y)

        # Get depth values (in mm, convert to meters)
        depth = depth_img[::step, ::step].astype(np.float32) / 1000.0

        # Camera intrinsics (approximate for Orbbec Astra)
        fx = fy = 525.0  # focal length
        cx = w / 2.0     # principal point x
        cy = h / 2.0     # principal point y

        # Convert to 3D
        valid = depth > 0.1  # Only include valid depths

        X = np.zeros_like(depth)
        Y = np.zeros_like(depth)
        Z = depth

        X[valid] = (xx[valid] - cx) * Z[valid] / fx
        Y[valid] = -(yy[valid] - cy) * Z[valid] / fy  # Flip Y for visualization

        # Color by depth
        colors = plt.cm.jet(depth / 5.0)  # Normalize to 5m range

        return X[valid], Y[valid], Z[valid], colors[valid]

    def run(self):
        """Main visualization loop"""
        fig = plt.figure(figsize=(12, 8))
        ax = fig.add_subplot(111, projection='3d')

        # Initial setup
        ax.set_xlabel('X (meters)')
        ax.set_ylabel('Y (meters)')
        ax.set_zlabel('Z (meters)')
        ax.set_title('3D Point Cloud - Submarine Vision')

        # Set initial view
        ax.view_init(elev=-30, azim=-90)

        # Scatter plot placeholder
        scatter = None

        print("\n3D Viewer active. Close window to exit.")

        def update(frame):
            nonlocal scatter

            with self.frame_lock:
                if self.depth_frame is not None:
                    depth_img = self.depth_frame.copy()
                else:
                    return

            try:
                X, Y, Z, colors = self.create_point_cloud(depth_img)

                # Remove old scatter
                if scatter is not None:
                    scatter.remove()

                # Create new scatter (downsample more for rendering)
                idx = np.random.choice(len(X), min(2000, len(X)), replace=False)
                scatter = ax.scatter(X[idx], Y[idx], Z[idx],
                                   c=colors[idx], s=1, alpha=0.6)

                # Set consistent axis limits
                ax.set_xlim(-3, 3)
                ax.set_ylim(-3, 3)
                ax.set_zlim(0, 5)

            except Exception as e:
                pass

            return scatter,

        # Create animation
        anim = FuncAnimation(fig, update, interval=100, cache_frame_data=False)

        plt.tight_layout()
        plt.show()

        self.running = False
        self.sock.close()
        print("\n3D Receiver closed")

def main():
    if len(sys.argv) < 2:
        print("Usage: submarine_3d_receiver.py <port>")
        sys.exit(1)

    port = int(sys.argv[1])
    receiver = DepthReceiver3D(port)
    receiver.run()

if __name__ == "__main__":
    main()
