import tkinter as tk
from tkinter import filedialog, messagebox
import cv2
import numpy as np
from PIL import Image, ImageTk
import os
import math

class MoonTunerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Moon Phase Tuner - Scrubber Edition")
        
        # Default Variables
        self.img_path = None
        self.original_cv_image = None
        self.display_image = None
        self.input_dir = ""
        self.all_image_files = [] # List to hold filenames
        self.current_index = 0
        
        # Tracking variables for tuning
        self.threshold_val = tk.IntVar(value=15)
        self.radius_val = tk.IntVar(value=100)
        self.offset_x = tk.IntVar(value=0)
        self.offset_y = tk.IntVar(value=0)

        # --- GUI LAYOUT ---
        
        # Control Panel (Left Side)
        control_frame = tk.Frame(root, width=350, padx=10, pady=10)
        control_frame.pack(side=tk.LEFT, fill=tk.Y)
        
        # 1. File Selection
        tk.Label(control_frame, text="1. Load Images", font=("Arial", 10, "bold")).pack(anchor="w", pady=(0, 5))
        tk.Button(control_frame, text="Select Input Folder", command=self.select_folder, height=2, bg="#ddd").pack(fill=tk.X, pady=5)
        
        self.lbl_status = tk.Label(control_frame, text="No folder selected", fg="gray")
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
        tk.Label(control_frame, text="3. Fine Tune Mask", font=("Arial", 10, "bold")).pack(anchor="w", pady=(15, 5))

        tk.Label(control_frame, text="Brightness Threshold").pack(anchor="w")
        tk.Scale(control_frame, from_=0, to=255, orient=tk.HORIZONTAL, 
                 variable=self.threshold_val, command=self.update_preview).pack(fill=tk.X)

        tk.Label(control_frame, text="Mask Radius").pack(anchor="w")
        self.radius_slider = tk.Scale(control_frame, from_=10, to=500, orient=tk.HORIZONTAL, 
                                      variable=self.radius_val, command=self.update_preview)
        self.radius_slider.pack(fill=tk.X)

        tk.Label(control_frame, text="Center Offset X").pack(anchor="w")
        tk.Scale(control_frame, from_=-100, to=100, orient=tk.HORIZONTAL, 
                 variable=self.offset_x, command=self.update_preview).pack(fill=tk.X)
        
        tk.Label(control_frame, text="Center Offset Y").pack(anchor="w")
        tk.Scale(control_frame, from_=-100, to=100, orient=tk.HORIZONTAL, 
                 variable=self.offset_y, command=self.update_preview).pack(fill=tk.X)

        # 4. Process Button
        tk.Label(control_frame, text="-----------------------").pack(pady=10)
        self.stats_label = tk.Label(control_frame, text="Illum: --%\nAngle: --°", font=("Courier", 14, "bold"), fg="#333")
        self.stats_label.pack(pady=10)
        
        tk.Button(control_frame, text="PROCESS ALL IMAGES", bg="green", fg="white", 
                  font=("Arial", 12, "bold"), command=self.process_all, height=2).pack(fill=tk.X, pady=20)

        # Image Display (Right Side)
        self.canvas_frame = tk.Frame(root, bg="#333")
        self.canvas_frame.pack(side=tk.RIGHT, expand=True, fill=tk.BOTH)
        
        self.canvas_label = tk.Label(self.canvas_frame, text="Load a folder to start", bg="#333", fg="white")
        self.canvas_label.pack(expand=True)

    def select_folder(self):
        d = filedialog.askdirectory()
        if d:
            self.input_dir = d
            # --- DEBUGGING START ---
            print(f"Scanning folder: {d}")
            all_files = os.listdir(d)
            print(f"Total files found: {len(all_files)}")
            print(f"First 5 files: {all_files[:5]}")
            # --- DEBUGGING END ---
            # Load all valid images
            valid_ext = ('.jpg', '.jpeg', '.png', '.bmp')
            self.all_image_files = sorted([f for f in os.listdir(d) if f.lower().endswith(valid_ext)])
            
            if not self.all_image_files:
                messagebox.showerror("Error", "No images found in this folder.")
                return

            # Setup Scrubber
            count = len(self.all_image_files)
            self.lbl_status.config(text=f"Loaded {count} images")
            self.scrubber.config(to=count - 1)
            self.scrubber.set(0)
            
            # Load the first image immediately
            self.load_image_by_index(0)

    def on_scrub(self, value):
        idx = int(value)
        self.load_image_by_index(idx)

    def prev_image(self):
        new_idx = max(0, self.current_index - 1)
        self.scrubber.set(new_idx) # This triggers on_scrub

    def next_image(self):
        new_idx = min(len(self.all_image_files) - 1, self.current_index + 1)
        self.scrubber.set(new_idx) # This triggers on_scrub

    def load_image_by_index(self, index):
        if not self.all_image_files: return
        
        self.current_index = index
        filename = self.all_image_files[index]
        self.img_path = os.path.join(self.input_dir, filename)
        
        self.lbl_filename.config(text=f"{index+1}/{len(self.all_image_files)}: {filename}")
        
        # Load OpenCV Image
        self.original_cv_image = cv2.imread(self.img_path)
        
        # Auto-guess radius ONLY if it's the very first time loading
        # Otherwise, preserve the user's manual slider position while scrubbing
        if self.radius_val.get() == 100 and self.original_cv_image is not None:
             h, w = self.original_cv_image.shape[:2]
             # Only auto-set if slider hasn't been touched much
             # (A bit of a hack, but prevents resetting user work on every scrub)
             pass 

        self.update_preview()

    def update_preview(self, _=None):
        if self.original_cv_image is None: return

        # Get settings
        thresh = self.threshold_val.get()
        r = self.radius_val.get()
        off_x = self.offset_x.get()
        off_y = self.offset_y.get()

        img = self.original_cv_image.copy()
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
        total = np.pi * (r ** 2)
        lit = cv2.countNonZero(lit_mask)
        frac = min(1.0, lit / total)
        pct = frac * 100
        
        # Angle Math
        ratio = 2 * frac - 1
        ratio = max(-1.0, min(1.0, ratio))
        angle = math.degrees(math.acos(ratio))
        
        self.stats_label.config(text=f"Illum: {pct:.2f}%\nAngle: {angle:.2f}°")

        # 4. Visualization
        # Yellow for lit part
        yellow_mask = np.zeros_like(img)
        yellow_mask[lit_mask == 255] = [0, 255, 255]
        
        # Blend
        preview = cv2.addWeighted(img, 0.8, yellow_mask, 0.4, 0)
        
        # Draw Geometry (Green Circle for mask)
        #cv2.circle(preview, (cx, cy), r, (0, 255, 0), 2)
        
        # Display
        preview_rgb = cv2.cvtColor(preview, cv2.COLOR_BGR2RGB)
        im_pil = Image.fromarray(preview_rgb)
        
        # Smart Resize (Fit to window height)
        max_h = 600
        aspect = im_pil.width / im_pil.height
        im_pil = im_pil.resize((int(max_h * aspect), max_h))
        
        self.display_image = ImageTk.PhotoImage(im_pil)
        self.canvas_label.config(image=self.display_image, text="")

    def process_all(self):
        if not self.all_image_files: return
        
        out_dir = os.path.join(self.input_dir, "processed_output")
        if not os.path.exists(out_dir): os.makedirs(out_dir)

        thresh = self.threshold_val.get()
        r = self.radius_val.get()
        off_x = self.offset_x.get()
        off_y = self.offset_y.get()
        
        processed_count = 0
        
        for f in self.all_image_files:
            path = os.path.join(self.input_dir, f)
            img = cv2.imread(path)
            if img is None: continue
            
            gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            h, w = gray.shape
            cx, cy = (w // 2) + off_x, (h // 2) + off_y
            
            # Logic matches preview exactly
            mask = np.zeros_like(gray)
            cv2.circle(mask, (cx, cy), r, 255, -1)
            masked_gray = cv2.bitwise_and(gray, gray, mask=mask)
            _, lit_mask = cv2.threshold(masked_gray, thresh, 255, cv2.THRESH_BINARY)
            
            total = np.pi * (r ** 2)
            lit = cv2.countNonZero(lit_mask)
            frac = min(1.0, lit / total)
            pct = frac * 100
            angle = math.degrees(math.acos(max(-1, min(1, 2*frac - 1))))
            
            # Annotate
            new_name = f"{processed_count:04d}_{pct:05.2f}_{angle:05.1f}.png"
            cv2.putText(img, f"Illum: {pct:.1f}%", (10, h-40), cv2.FONT_HERSHEY_SIMPLEX, 1, (0,255,0), 2)
            cv2.putText(img, f"Angle: {angle:.1f} deg", (10, h-10), cv2.FONT_HERSHEY_SIMPLEX, 1, (0,255,0), 2)
            cv2.circle(img, (cx, cy), r, (0, 255, 0), 2)
            
            cv2.imwrite(os.path.join(out_dir, new_name), img)
            processed_count += 1
            print(f"Saved {new_name}")

        messagebox.showinfo("Success", f"Processed {processed_count} images!\nCheck the 'processed_output' folder.")

if __name__ == "__main__":
    root = tk.Tk()
    app = MoonTunerApp(root)
    root.mainloop()