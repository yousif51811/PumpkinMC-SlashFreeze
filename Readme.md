# Freeze!
A lightweight and simple PumpkinMC plugin to restrict player movement.

## Permissions

### `slashfreeze:freeze`

Allows executing the /freeze and /unfreeze commands
Granted to operators by default.

## Commands

### `/freeze [player(s)]`

### `/unfreeze [player(s)]`

## Prerequisites

### [PumpkinMC API for C](https://github.com/Pumpkin-MC/pumpkin-api-c/releases/)

Configure the directory in [build.sh](/build.sh)

### [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34)

Should be in your path, or configure build.sh to it's path

## Building

Simply run build.sh or run:

```shell
/path/to/wasi-sdk/bin/wasm32-wasip2-clang -Oz \
    -Ipumpkin-api/include \
    pumpkin-api/lib/libpumpkin-api.a \
    main.c \
    -o my_plugin.wasm \
    -mexec-model=reactor
```

---------
**Made with ❤️ by yousif51811 (Zerorifter)**