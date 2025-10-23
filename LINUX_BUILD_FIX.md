# Linux Build Fix

## Problem

If you see linker errors like:
```
undefined reference to `FileOperationsHandler::listFileLocations(std::__cxx11::basic_string<char...`
```

This is caused by GCC's dual ABI for `std::string`.

## Solution

The CMakeLists.txt has been updated with the fix. Just rebuild:

```bash
cd /Users/rayansingh/cs425_mp3
rm -rf build
mkdir build
cd build
cmake ..
make -j4
```

## What Was Fixed

Added this line to `CMakeLists.txt`:
```cmake
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)
```

This ensures all code uses the C++11 ABI consistently.

## Alternative Fix (If Above Doesn't Work)

If you still have issues, try:

```bash
cd build
cmake .. -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0"
make clean
make -j4
```

Or the opposite:
```bash
cmake .. -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1"
make clean
make -j4
```

## Verify Build

After building, verify it works:

```bash
./main localhost 12345
```

You should see:
```
[timestamp]: Added node: localhost:12345:timestamp in mode: PINGACK
```

Then you can type commands like:
- `list_self`
- `list_mem`
- `create test.txt file.txt`
- etc.

## Still Having Issues?

Check your GCC version:
```bash
g++ --version
```

Required: GCC 7+ or Clang 5+

Make sure you're using a consistent compiler throughout.
