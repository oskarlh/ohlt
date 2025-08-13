#!/bin/sh

crossover="/Applications/CrossOver.app" # Where you keep CrossOver
bottle="Steam"

open "$crossover"

export DISPLAY=:`defaults read com.codeweavers.CrossOver Display`
export DYLD_FALLBACK_LIBRARY_PATH="$crossover/Contents/SharedSupport/X11/lib:$HOME/lib:/lib:/usr/lib:/usr/X11/lib"
export FONT_ENCODINGS_DIRECTORY="$crossover/Contents/SharedSupport/X11/lib/X11/fonts/encodings/encodings.dir"
export FONTCONFIG_PATH="$crossover/Contents/SharedSupport/X11/etc/fonts"
export FONTCONFIG_ROOT="$crossover/Contents/SharedSupport/X11"
export VERSIONER_PERL_PREFER_32_BIT=yes
export CX_BOTTLE="$bottle"
export PATH="$crossover/Contents/SharedSupport/CrossOver/bin:$PATH"

(
	cd "$HOME/Library/Application Support/CrossOver/Bottles/$bottle/drive_c" &&
	wine "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/hl.exe" $@
)
