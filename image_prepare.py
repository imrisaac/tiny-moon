import tkinter as tk
from tkinter import filedialog, messagebox
import cv2
import numpy as np
from PIL import Image, ImageTk
import os
import math
import requests
from concurrent.futures import ThreadPoolExecutor, as_completed


class ImagePreparer:
    """Handles downloading frames and preparing cropped 1080x1080 images."""

    def __init__(self):
        self.url_pattern = (
            "https://svs.gsfc.nasa.gov/vis/a000000/a004300/a004310/frames/"
            "1920x1080_16x9_30p/moon.[0001-0236].tif"
        )
        self.total_frames = 236

    def _status(self, callback, message):
        if callback:
            callback(message)

    def download_frames(self, target_dir, status_callback=None, max_workers=12):
        os.makedirs(target_dir, exist_ok=True)
        to_download = []

        for i in range(1, self.total_frames + 1):
            filename = os.path.join(target_dir, f"moon.{i:04d}.tif")
            if not os.path.exists(filename):
                to_download.append((i, filename))

        if not to_download:
            self._status(status_callback, "All frames already downloaded.")
            return 0, []

        downloaded = 0
        failures = []
        total = len(to_download)

        def fetch(entry):
            idx, filename = entry
            url = self.url_pattern.replace("[0001-0236]", f"{idx:04d}")
            try:
                response = requests.get(url, timeout=30)
                response.raise_for_status()
            except requests.RequestException as exc:
                return False, idx, str(exc)

            with open(filename, "wb") as fh:
                fh.write(response.content)
            return True, idx, filename

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_map = {executor.submit(fetch, entry): entry for entry in to_download}
            for future in as_completed(future_map):
                success, idx, info = future.result()
                if success:
                    downloaded += 1
                    self._status(
                        status_callback,
                        f"Downloaded {downloaded}/{total} frame(s)"
                    )
                else:
                    failures.append((idx, info))
                    self._status(status_callback, f"Failed {idx:04d}: {info}")

        return downloaded, failures

    def load_prepared_cv_image(self, path):
        """Load, crop, and resize an image, returning a BGR array."""
        try:
            with Image.open(path) as original_image:
                prepared = self._crop_and_resize(original_image)
                prepared = prepared.convert("RGB")
                rgb_array = np.array(prepared)
        except OSError as exc:
            raise RuntimeError(f"Failed to load {path}: {exc}") from exc

        # Convert RGB -> BGR for OpenCV operations
        return cv2.cvtColor(rgb_array, cv2.COLOR_RGB2BGR)

    def _crop_and_resize(self, image):
        width, height = image.size
        crop_size = min(width, height)
        left = (width - crop_size) // 2
        upper = (height - crop_size) // 2
        right = left + crop_size
        lower = upper + crop_size
        cropped_image = image.crop((left, upper, right, lower))

        try:
            resample_filter = Image.Resampling.LANCZOS
        except AttributeError:
            resample_filter = Image.LANCZOS

        return cropped_image.resize((1080, 1080), resample=resample_filter)


class MoonTunerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Moon Phase Tuner - Scrubber Edition")

        self.preparer = ImagePreparer()
        self.prepared_cache = {}

        # Default Variables
        self.img_path = None
        self.original_cv_image = None
        self.display_image = None
        self.input_dir = ""
        self.all_image_files = []  # List to hold filenames
        self.current_index = 0

        self.raw_dir = tk.StringVar(value=os.path.abspath("raw_images"))
        # Tracking variables for tuning
        self.threshold_val = tk.IntVar(value=15)
        self.radius_val = tk.IntVar(value=200)
        self.offset_x = tk.IntVar(value=0)
        self.offset_y = tk.IntVar(value=0)
        self.zoom_percent = tk.IntVar(value=100)
        self.crop_percent = tk.IntVar(value=100)

        # --- GUI LAYOUT ---

        # Control Panel (Left Side)
        control_frame = tk.Frame(root, width=360, padx=10, pady=10)
        control_frame.pack(side=tk.LEFT, fill=tk.Y)

        # 1. Preparation / Selection
        tk.Label(control_frame, text="1. Download or Select Images", font=("Arial", 10, "bold")).pack(anchor="w", pady=(0, 5))
        self._build_dir_selector(control_frame, "Image folder", self.raw_dir, self.browse_raw_dir)

        tk.Button(control_frame, text="Download NASA Frames", command=self.download_images, height=1, bg="#ccc").pack(fill=tk.X, pady=2)
        tk.Button(control_frame, text="Load Selected Folder", command=self.load_current_folder, height=2, bg="#ddd").pack(fill=tk.X, pady=5)

        self.lbl_status = tk.Label(control_frame, text="No folder loaded", fg="gray")
        self.lbl_status.pack(pady=2)

        # 2. Scrubber (Navigation)
        tk.Label(control_frame, text="2. Scrub Timeline", font=("Arial", 10, "bold")).pack(anchor="w", pady=(15, 5))

        nav_frame = tk.Frame(control_frame)
        nav_frame.pack(fill=tk.X)

        tk.Button(nav_frame, text="<", command=self.prev_image, width=3).pack(side=tk.LEFT)
        self.scrubber = tk.Scale(nav_frame, from_=0, to=0, orient=tk.HORIZONTAL, showvalue=0, command=self.on_scrub)
        self.scrubber.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        tk.Button(nav_frame, text=">", command=self.next_image, width=3).pack(side=tk.RIGHT)

        self.lbl_filename = tk.Label(control_frame, text="--", font=("Arial", 9))
        self.lbl_filename.pack(pady=2)

        # 3. Tuning Controls
        tk.Label(control_frame, text="3. Fine Tune Mask", font=("Arial", 10, "bold")).pack(anchor="w", pady=(30, 5))

        self._add_slider(
            control_frame,
            "Brightness Threshold",
            from_=0,
            to=255,
            orient=tk.HORIZONTAL,
            variable=self.threshold_val,
            command=self.update_preview,
        )

        self._add_slider(
            control_frame,
            "Mask Radius",
            from_=10,
            to=600,
            orient=tk.HORIZONTAL,
            variable=self.radius_val,
            command=self.update_preview,
        )

        self._add_slider(
            control_frame,
            "Center Offset X",
            from_=-100,
            to=100,
            orient=tk.HORIZONTAL,
            variable=self.offset_x,
            command=self.update_preview,
        )

        self._add_slider(
            control_frame,
            "Center Offset Y",
            from_=-100,
            to=100,
            orient=tk.HORIZONTAL,
            variable=self.offset_y,
            command=self.update_preview,
        )

        self._add_slider(
            control_frame,
            "Preview Zoom (%)",
            from_=50,
            to=200,
            orient=tk.HORIZONTAL,
            variable=self.zoom_percent,
            command=self.update_preview,
        )

        self._add_slider(
            control_frame,
            "Output Crop (%)",
            from_=50,
            to=100,
            orient=tk.HORIZONTAL,
            variable=self.crop_percent,
            command=self.update_preview,
        )

        # 4. Process Button
        tk.Label(control_frame, text="-----------------------").pack(pady=10)
        self.stats_label = tk.Label(control_frame, text="Illum: --%\nAngle: --°", font=("Courier", 14, "bold"), fg="#333")
        self.stats_label.pack(pady=10)

        tk.Button(control_frame, text="PROCESS ALL IMAGES", bg="green", fg="white",
                  font=("Arial", 12, "bold"), command=self.process_all, height=2).pack(fill=tk.X, pady=20)

        # Image Display (Right Side)
        self.canvas_frame = tk.Frame(root, bg="#333")
        self.canvas_frame.pack(side=tk.RIGHT, expand=True, fill=tk.BOTH)

        self.canvas_label = tk.Label(self.canvas_frame, text="Load images to start", bg="#333", fg="white")
        self.canvas_label.pack(expand=True)

    # ---- Directory helpers ----
    def _build_dir_selector(self, parent, label_text, variable, browse_command):
        frame = tk.Frame(parent)
        frame.pack(fill=tk.X, pady=2)
        tk.Label(frame, text=label_text).pack(anchor="w")
        entry_frame = tk.Frame(frame)
        entry_frame.pack(fill=tk.X)
        tk.Entry(entry_frame, textvariable=variable).pack(side=tk.LEFT, fill=tk.X, expand=True)
        tk.Button(entry_frame, text="...", width=4, command=browse_command).pack(side=tk.RIGHT, padx=(5, 0))

    def _add_slider(self, parent, label_text, **scale_kwargs):
        frame = tk.Frame(parent)
        frame.pack(fill=tk.X, pady=(8, 0))
        scale = tk.Scale(frame, **scale_kwargs)
        scale.pack(fill=tk.X)
        tk.Label(frame, text=label_text).pack(anchor="w", pady=(2, 0))
        return scale

    def browse_raw_dir(self):
        selected = filedialog.askdirectory(initialdir=self.raw_dir.get())
        if selected:
            self.raw_dir.set(selected)

    def load_current_folder(self):
        directory = self.raw_dir.get()
        if not directory:
            messagebox.showerror("Load", "Please set an image folder first.")
            return
        self.load_from_directory(directory)

    def set_status(self, text):
        self.lbl_status.config(text=text)
        self.root.update_idletasks()

    # ---- Image acquisition ----
    def download_images(self):
        target = self.raw_dir.get()
        if not target:
            messagebox.showerror("Download", "Please set a raw frames folder first.")
            return

        self.set_status("Downloading NASA frames...")
        downloaded, failures = self.preparer.download_frames(target, self.set_status)
        summary = f"Downloaded {downloaded} new frame(s) to:\n{target}"
        if failures:
            summary += f"\nFailed {len(failures)} frame(s). Check logs for details."
        messagebox.showinfo("Download complete", summary)
        self.set_status(summary)

        # Automatically refresh the folder so new frames are available for tuning
        self.load_from_directory(target)

    def load_from_directory(self, directory):
        if not os.path.isdir(directory):
            messagebox.showerror("Load", "Selected directory does not exist.")
            return

        valid_ext = (".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff")
        files = sorted([f for f in os.listdir(directory) if f.lower().endswith(valid_ext)])

        if not files:
            messagebox.showerror("Load", "No compatible images found in this folder.")
            return

        self.input_dir = directory
        self.all_image_files = files
        self.prepared_cache.clear()

        count = len(files)
        self.lbl_status.config(text=f"Loaded {count} images")
        self.scrubber.config(to=count - 1)
        self.scrubber.set(0)
        self.load_image_by_index(0)

    def _get_prepared_image(self, path):
        cached = self.prepared_cache.get(path)
        if cached is None:
            cached = self.preparer.load_prepared_cv_image(path)
            self.prepared_cache[path] = cached
        return cached.copy()

    # ---- Navigation ----
    def on_scrub(self, value):
        idx = int(value)
        self.load_image_by_index(idx)

    def prev_image(self):
        new_idx = max(0, self.current_index - 1)
        self.scrubber.set(new_idx)  # This triggers on_scrub

    def next_image(self):
        new_idx = min(len(self.all_image_files) - 1, self.current_index + 1)
        self.scrubber.set(new_idx)  # This triggers on_scrub

    # ---- Preview / Processing ----
    def load_image_by_index(self, index):
        if not self.all_image_files:
            return

        self.current_index = index
        filename = self.all_image_files[index]
        self.img_path = os.path.join(self.input_dir, filename)

        self.lbl_filename.config(text=f"{index + 1}/{len(self.all_image_files)}: {filename}")

        try:
            self.original_cv_image = self._get_prepared_image(self.img_path)
        except RuntimeError as exc:
            messagebox.showerror("Load", str(exc))
            return

        self.update_preview()

    def update_preview(self, _=None):
        if self.original_cv_image is None:
            return

        # Get settings
        thresh = self.threshold_val.get()
        r = self.radius_val.get()
        off_x = self.offset_x.get()
        off_y = self.offset_y.get()

        img = self._apply_user_crop(self.original_cv_image.copy())
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        h, w = gray.shape
        cx, cy = (w // 2) + off_x, (h // 2) + off_y

        # 1. Mask
        mask = np.zeros_like(gray)
        cv2.circle(mask, (cx, cy), r, 255, -1)
        masked_gray = cv2.bitwise_and(gray, gray, mask=mask)

        # 2. Threshold
        _, lit_mask = cv2.threshold(masked_gray, thresh, 255, cv2.THRESH_BINARY)

        # 3. Stats
        total = math.pi * (r ** 2)
        lit = cv2.countNonZero(lit_mask)
        frac = min(1.0, lit / total)
        pct = frac * 100

        # Angle Math
        ratio = 2 * frac - 1
        ratio = max(-1.0, min(1.0, ratio))
        angle = math.degrees(math.acos(ratio))

        self.stats_label.config(text=f"Illum: {pct:.2f}%\nAngle: {angle:.2f}°")

        # 4. Visualization
        yellow_mask = np.zeros_like(img)
        yellow_mask[lit_mask == 255] = [0, 255, 255]
        preview = cv2.addWeighted(img, 0.8, yellow_mask, 0.4, 0)

        preview_rgb = cv2.cvtColor(preview, cv2.COLOR_BGR2RGB)
        im_pil = Image.fromarray(preview_rgb)

        base_h = 600
        scale = max(0.1, self.zoom_percent.get() / 100)
        target_h = max(50, int(base_h * scale))
        aspect = im_pil.width / im_pil.height
        im_pil = im_pil.resize((int(target_h * aspect), target_h))

        self.display_image = ImageTk.PhotoImage(im_pil)
        self.canvas_label.config(image=self.display_image, text="")

    def process_all(self):
        if not self.all_image_files:
            return

        out_dir = os.path.join(self.input_dir, "processed_output")
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        thresh = self.threshold_val.get()
        r = self.radius_val.get()
        off_x = self.offset_x.get()
        off_y = self.offset_y.get()

        processed_count = 0

        for f in self.all_image_files:
            path = os.path.join(self.input_dir, f)
            try:
                img = self._get_prepared_image(path)
            except RuntimeError as exc:
                print(str(exc))
                continue

            img = self._apply_user_crop(img)
            gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            h, w = gray.shape
            cx, cy = (w // 2) + off_x, (h // 2) + off_y

            mask = np.zeros_like(gray)
            cv2.circle(mask, (cx, cy), r, 255, -1)
            masked_gray = cv2.bitwise_and(gray, gray, mask=mask)
            _, lit_mask = cv2.threshold(masked_gray, thresh, 255, cv2.THRESH_BINARY)

            total = math.pi * (r ** 2)
            lit = cv2.countNonZero(lit_mask)
            frac = min(1.0, lit / total)
            pct = frac * 100
            angle = math.degrees(math.acos(max(-1, min(1, 2 * frac - 1))))

            new_name = f"{processed_count:04d}_{pct:05.2f}_{angle:05.1f}.png"
            #cv2.putText(img, f"Illum: {pct:.1f}%", (10, h - 40), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
            #cv2.putText(img, f"Angle: {angle:.1f} deg", (10, h - 10), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

            cv2.imwrite(os.path.join(out_dir, new_name), img)
            processed_count += 1
            print(f"Saved {new_name}")

        messagebox.showinfo("Success", f"Processed {processed_count} images!\nCheck the processed_output folder.")

    def _apply_user_crop(self, image):
        """Crop to a centered 1:1 region scaled by the user's percentage."""
        height, width = image.shape[:2]
        if height != width:
            image = self._center_square_crop(image)
            height, width = image.shape[:2]

        percent = max(1, min(100, self.crop_percent.get()))
        if percent == 100:
            return image

        side = max(1, int(min(height, width) * (percent / 100.0)))
        top = (height - side) // 2
        left = (width - side) // 2
        return image[top:top + side, left:left + side].copy()

    @staticmethod
    def _center_square_crop(image):
        """Crop the np.ndarray image to a centered 1:1 aspect ratio."""
        height, width = image.shape[:2]
        if height == width:
            return image

        side = min(height, width)
        top = (height - side) // 2
        left = (width - side) // 2
        return image[top:top + side, left:left + side]


if __name__ == "__main__":
    root = tk.Tk()
    app = MoonTunerApp(root)
    root.mainloop()
