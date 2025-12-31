#!/bin/sh
set -eu

build_dir="build"

if [ -d "$build_dir" ]; then
    printf '%s\n' "Directory '$build_dir/' already exists."
    printf '%s' "Select no to keep it and skip clean build (WARNING: Default is yes!) [Y/n]: "

    if read -r answer; then
        case "$answer" in
            [nN]|[nN][oO])
                printf '%s\n' "Keeping existing '$build_dir'."
                ;;
            *)
                printf '%s\n' "Removing '$build_dir'..."
                rm -rf "$build_dir"
                ;;
        esac
    else
        printf '%s\n' "No input received; performing clean build."
        rm -rf "$build_dir"
    fi
fi

cmake -S . -B "$build_dir"
cmake --build "$build_dir" -j

printf '%s\n' "Successfully build. You can now run: ./$build_dir/dmt"
