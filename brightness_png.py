import os
from PIL import Image
import requests

# Directory path containing PNG images
image_directory = 'cropped_images'

# Brightness threshold (0-255)
brightness_threshold = 150

# Function to calculate the percentage of pixels above the threshold
def calculate_brightness_percentage(image_path):
    image = Image.open(image_path)
    width, height = image.size
    total_pixels = width * height
    bright_pixels = 0

    for x in range(width):
        for y in range(height):
            pixel = image.getpixel((x, y))
            brightness = sum(pixel) / 3  # Calculate average brightness
            if brightness > brightness_threshold:
                bright_pixels += 1

    percentage = (bright_pixels / total_pixels) * 100
    return percentage

# Loop through PNG files in the directory
total_percentage = 0
num_images = 0

for filename in os.listdir(image_directory):
    if filename.lower().endswith('.png'):
        image_path = os.path.join(image_directory, filename)
        percentage = calculate_brightness_percentage(image_path)
        total_percentage += percentage
        num_images += 1
        print(f"Image {filename}: Brightness Percentage = {percentage:.2f}%")

if num_images > 0:
    average_percentage = total_percentage / num_images
    print(f"Average Brightness Percentage: {average_percentage:.2f}%")
else:
    print("No PNG images found in the directory.")
