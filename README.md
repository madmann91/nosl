# NOSL

NOSL is not OSL, but is another OSL implementation that tries to improve the OSL compiler. It
remains completely compatible with the standard OSL implementation.

## Status

- [ ] Preprocessor
    - `#line` and `#pragma once` support is missing
    - `__LINE__`, `__FILE__` are missing
- [X] Parser
- [ ] Type-checker
    - Texture-related built-in functions are missing
    - Noise-related built-in functions are missing
- [ ] Backend
    - Not yet implemented

## Building

Make sure to download and update submodules before building:

    git submodule update --init

### Requirements

- CMake 3.16 or higher
- C compiler with C23 support

### Instructions

Just run the following commands:

    mkdir build
    cd build
    cmake ..
    cmake --build .

## Testing

The project can be tested using CTest. The command is simply:

    ctest

When the `Coverage` build configuration is enabled, a coverage report can be generated via (this
requires `gcovr`):

    cmake --build . --target coverage

## License

This project is distributed under the GPL-3.0 license. See LICENSE.txt.
If you wish to use the source code of this project under a different license, please contact me.
