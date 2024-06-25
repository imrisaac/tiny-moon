import os
import shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import matplotlib.pyplot as plt

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

def add_text_to_image(image_path, text, output_path):
    img = Image.open(image_path)
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()

    # Calculate text size and position
    text_size = draw.textsize(text, font=font)
    text_x = img.width - text_size[0] - 10
    text_y = img.height - text_size[1] - 10

    # Add text to image
    draw.text((text_x, text_y), text, font=font, fill="white")
    img.save(output_path)

def process_images_in_directory(directory_path, output_directory, threshold):
    if not os.path.exists(output_directory):
        os.makedirs(output_directory)

    filenames = sorted(os.listdir(directory_path))
    for i, filename in enumerate(filenames):
        if filename.endswith(".png"):
            image_path = os.path.join(directory_path, filename)
            illumination_percentage = calculate_illumination_percentage(image_path, threshold)
            # Format the illumination percentage to avoid periods in the filename
            formatted_percentage = f"{illumination_percentage:.2f}".replace(".", "_")
            new_filename = f"{i+1:04d}_{formatted_percentage}.png"
            new_image_path = os.path.join(output_directory, new_filename)
            # Add illumination percentage text to the image
            add_text_to_image(image_path, f"{illumination_percentage:.2f}%", new_image_path)
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

    process_images_in_directory(directory_path, output_directory, threshold)
