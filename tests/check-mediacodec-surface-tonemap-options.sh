#!/bin/sh
set -eu

ffmpeg=${1:-./ffmpeg}

help=$("$ffmpeg" -hide_banner -h encoder=h264_mediacodec 2>&1)

require() {
    needle=$1
    if ! printf '%s\n' "$help" | grep -F -- "$needle" >/dev/null; then
        printf '%s\n' "missing h264_mediacodec option: $needle" >&2
        exit 1
    fi
}

require "surface_tonemap"
require "surface_tonemap_algorithm"
require "surface_tonemap_transfer"
require "surface_tonemap_matrix"
require "surface_tonemap_primaries"
require "surface_tonemap_range"
require "surface_tonemap_format"
require "surface_tonemap_param"
require "surface_tonemap_desat"
require "surface_tonemap_peak"
require "bt2390"
require "reinhard"
require "mobius"
require "smpte2084"
require "yuv420p"

printf '%s\n' "h264_mediacodec surface tonemap options are exposed"
