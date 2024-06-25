import os
import shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import matplotlib.pyplot as plt
from scipy.interpolate import interp1d

def create_circular_mask(image_shape, center=None, radius=None):
    h, w = image_shape
    if center is None:  # use the middle of the image
        center = (int(w / 2), int(h / 2))
    if radius is None:  # use the smallest distance from the center to an edge
        radius = min(center[0], center[1], w - center[0], h - center[1])

    Y, X = np.ogrid[:h, :w]
    dist_from_center = np.sqrt((X - center[0]) ** 2 + (Y - center[1]) ** 2)

    mask = dist_from_center <= radius
    return mask

def apply_threshold(image_array, threshold):
    return image_array > threshold

def visualize_thresholds_for_images(image_paths, thresholds):
    num_images = len(image_paths)
    fig, axes = plt.subplots(num_images, len(thresholds) + 1, figsize=(15, 5 * num_images))
    
    for row, image_path in enumerate(image_paths):
        img = Image.open(image_path).convert('L')
        img_array = np.array(img)

        mask = create_circular_mask(img_array.shape, radius=110)  # Adjust radius if necessary
        masked_img_array = np.where(mask, img_array, 0)

        axes[row, 0].imshow(masked_img_array, cmap='gray')
        axes[row, 0].set_title('Original (Masked)')

        for col, threshold in enumerate(thresholds):
            illuminated = apply_threshold(masked_img_array, threshold)
            axes[row, col + 1].imshow(illuminated, cmap='gray')
            axes[row, col + 1].set_title(f'Threshold: {threshold}')

    plt.tight_layout()
    plt.show()

def calculate_illumination_percentage(image_path, threshold):
    img = Image.open(image_path).convert('L')  # Convert to grayscale
    img_array = np.array(img)

    mask = create_circular_mask(img_array.shape, radius=110)  # radius is half of the diameter (230 pixels)
    masked_img_array = np.where(mask, img_array, 0)

    illuminated = masked_img_array > threshold

    total_pixels = np.sum(mask)
    illuminated_pixels = np.sum(illuminated)
    illumination_percentage = (illuminated_pixels / total_pixels) * 100

    return illumination_percentage

def add_text_to_image(image_path, illumination_text, angle_text, output_path):
    img = Image.open(image_path)
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()  # Use default font provided by Pillow

    # Calculate text size and position for illumination percentage
    illumination_text_size = draw.textsize(illumination_text, font=font)
    illumination_text_x = 10
    illumination_text_y = img.height - illumination_text_size[1] - 10

    # Calculate text size and position for moon phase angle
    angle_text_size = draw.textsize(angle_text, font=font)
    angle_text_x = img.width - angle_text_size[0] - 10
    angle_text_y = img.height - angle_text_size[1] - 10

    # Add text to image
    draw.text((illumination_text_x, illumination_text_y), illumination_text, font=font, fill="white")
    draw.text((angle_text_x, angle_text_y), angle_text, font=font, fill="white")
    img.save(output_path)

def process_images_in_directory(directory_path, output_directory, threshold, labeled_phases):
    if not os.path.exists(output_directory):
        os.makedirs(output_directory)

    filenames = sorted(os.listdir(directory_path))
    
    # Extract indices and labeled phase angles
    labeled_indices = sorted([filenames.index(k) for k in labeled_phases.keys()])
    labeled_angles = [labeled_phases[filenames[idx]] for idx in labeled_indices]

    # Create an interpolation function
    interp_function = interp1d(labeled_indices, labeled_angles, kind='linear', fill_value='extrapolate')

    for i, filename in enumerate(filenames):
        if filename.endswith(".png"):
            image_path = os.path.join(directory_path, filename)
            illumination_percentage = calculate_illumination_percentage(image_path, threshold)
            phase_angle = interp_function(i)  # Interpolate phase angle
            # Format the illumination percentage and phase angle to avoid periods in the filename
            formatted_percentage = f"{illumination_percentage:.2f}".replace(".", "_")
            formatted_phase_angle = f"{phase_angle:.2f}".replace(".", "_")
            new_filename = f"{i+1:04d}_{formatted_percentage}_{formatted_phase_angle}.png"
            new_image_path = os.path.join(output_directory, new_filename)
            # Add illumination percentage and moon phase angle text to the image
            add_text_to_image(image_path, f"{illumination_percentage:.2f}%", f"{phase_angle:.2f}°", new_image_path)
            print(f"Processed {filename}: {illumination_percentage:.2f}% -> {new_filename}")

if __name__ == "__main__":
    directory_path = "cropped_images"  # Replace with the path to your images
    output_directory = "processed_images"  # Replace with the desired output directory

    # Predefined list of filenames for visualization
    sample_filenames = ["0041.png", "0114.png", "0235.png"]
    sample_image_paths = [os.path.join(directory_path, filename) for filename in sample_filenames]

    thresholds = [10, 11, 12, 13, 14, 15]  # List of thresholds to test
    visualize_thresholds_for_images(sample_image_paths, thresholds)

    threshold = int(input("Enter the chosen threshold value: "))

    # Manually labeled phase angles for specific images
    labeled_phases = {
        "0001.png": 0,
        "0060.png": 90,
        "0115.png": 180,
        "0177.png": 270,
        "0235.png": 359
    }

    process_images_in_directory(directory_path, output_directory, threshold, labeled_phases)