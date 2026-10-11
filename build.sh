#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
API_DIR=${PUMPKIN_API_DIR:-"$SCRIPT_DIR/../pumpkin-api"}

if [ ! -r "$API_DIR/lib/libpumpkin-api.a" ]; then
    printf 'Cannot read API archive: %s/lib/libpumpkin-api.a\n' "$API_DIR" >&2
    exit 1
fi

cd "$SCRIPT_DIR"
wasm32-wasip2-clang -Oz \
    -I"$API_DIR/include" \
    "$API_DIR/lib/libpumpkin-api.a" \
    main.c \
    -o SlashFreeze.wasm \
    -mexec-model=reactor
