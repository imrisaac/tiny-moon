import os
from PIL import Image
import requests

source_directory = 'raw_images/'
destination_directory = 'cropped_images_2/'

url_pattern = "https://svs.gsfc.nasa.gov/vis/a000000/a004300/a004310/frames/1920x1080_16x9_30p/moon.[0001-0236].tif"

# Ensure the destination and source directories exist
os.makedirs(destination_directory, exist_ok=True)
os.makedirs(source_directory, exist_ok=True)

# List to hold processed images
processed_images = []

def calculate_brightness_percentage(image, brightness_threshold):
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

for i in range(1, 236):
    url = url_pattern.replace("[0001-0236]", f"{i:04d}")
    response = requests.get(url)
    if response.status_code == 200:
        filename = os.path.join(source_directory, f"moon.{i:04d}.tif")
        with open(filename, "wb") as file:
            file.write(response.content)
        print(f"Downloaded: moon.{i:04d}.tif")
    else:
        print(f"Failed to download: moon.{i:04d}.tif")

# Loop through all files in the source directory
for filename in os.listdir(source_directory):
    if filename.lower().endswith('.tiff') or filename.lower().endswith('.tif'):
        source_path = os.path.join(source_directory, filename)
        destination_path = os.path.join(destination_directory, filename)

        # Load the original image
        original_image = Image.open(source_path)

        # Get the dimensions of the original image
        width, height = original_image.size

        # Calculate the coordinates for cropping
        left = (width - 1080) // 2
        upper = (height - 1080) // 2
        right = left + 1080
        lower = upper + 1080

        # Crop the image
        cropped_image = original_image.crop((left, upper, right, lower))

        # Resize the cropped image to 240x240
        resized_image = cropped_image.resize((240, 240), Image.ANTIALIAS)

        # brightness_threshold = 15
        # brightness_percentage = calculate_brightness_percentage(resized_image, brightness_threshold)
        # print(f"Brightness Percentage: {brightness_percentage:.2f}%")

        # Generate a custom filename for the PNG image
        number_part = filename.split('.')[1]
        png_filename = f'{number_part}.png'
        # png_filename = f'{os.path.splitext(png_filename)[5:]}.png'

        # Save the processed image as PNG
        png_path = os.path.join(destination_directory, png_filename)
        resized_image.save(png_path, format='PNG')

print("Cropping completed.")
