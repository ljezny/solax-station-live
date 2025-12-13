#!/bin/bash
# Font generation script for Solar Station Live
# Generates LVGL fonts with Czech, German character support
#
# Requirements:
#   npm install -g lv_font_conv
#
# Usage:
#   ./scripts/generate_fonts.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FONT_DIR="$PROJECT_DIR/SquareLine/assets"
OUTPUT_DIR="$PROJECT_DIR/src/ui"

# Font file
FONT_REGULAR="$FONT_DIR/OpenSans-SemiBold.ttf"
FONT_BOLD="$FONT_DIR/OpenSans-Bold.ttf"

# Character ranges:
# 0x20-0xFF    = Basic Latin + Latin-1 Supplement (includes German äöüÄÖÜß)
# 0x100-0x17F  = Latin Extended-A (includes Czech ČčŘřŠšŽžĚěĎďŤťŇňÚúŮů)
CHAR_RANGE="0x20-0xFF,0x100-0x17F"

# Common options
COMMON_OPTS="--format lvgl --no-compress --no-prefilter"

echo "=== Generating LVGL fonts with Czech/German support ==="
echo "Font source: $FONT_REGULAR"
echo "Character range: $CHAR_RANGE"
echo ""

# Function to generate a font
generate_font() {
    local name=$1
    local size=$2
    local font=$3
    local output="$OUTPUT_DIR/ui_font_${name}.c"
    
    echo "Generating: $name (${size}px)"
    
    lv_font_conv \
        --bpp 4 \
        --size $size \
        --font "$font" \
        -r "$CHAR_RANGE" \
        $COMMON_OPTS \
        -o "$output"
    
    echo "  -> $output"
}

# Generate all fonts
# Note: Sizes should match SquareLine Studio configuration

generate_font "OpenSansExtraSmall" 12 "$FONT_REGULAR"
generate_font "OpenSansSmall" 14 "$FONT_REGULAR"
generate_font "OpenSansMedium" 20 "$FONT_REGULAR"
generate_font "OpenSansMediumBold" 26 "$FONT_BOLD"
generate_font "OpenSansLarge" 36 "$FONT_REGULAR"
generate_font "OpenSansLargeBold" 36 "$FONT_BOLD"

echo ""
echo "=== Done! ==="
echo ""
echo "NOTE: The new fonts may have larger line_height due to diacritics."
echo "If you see vertical alignment issues, you may need to adjust"
echo "UI element heights or use lv_obj_set_style_text_line_space()."
echo ""
echo "Original line heights (for reference):"
echo "  OpenSansExtraSmall: 16"
echo "  OpenSansSmall: 18"
echo "  OpenSansMedium: 20"
echo "  OpenSansMediumBold: 27"
echo "  OpenSansLarge: 37"
echo "  OpenSansLargeBold: 37"
