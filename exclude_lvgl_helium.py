"""
PlatformIO extra script to exclude LVGL Helium ARM-specific files
from compilation on non-ARM platforms (like ESP32/Xtensa).
"""
Import("env")

# Get the library builder
def exclude_helium_files(env, node):
    """Filter out Helium assembly and source files"""
    path = node.get_path()
    if "helium" in path.lower():
        return None  # Exclude this file
    return node

# Apply the filter to library source files
env.AddBuildMiddleware(exclude_helium_files)
