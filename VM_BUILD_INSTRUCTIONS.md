# Building on VMs - FIXED

## The Problem

The Makefile was missing the two new source files:
- `src/file_message.cpp`
- `src/file_operations_handler.cpp`

This has been **FIXED** in the Makefile.

## How to Build on Your VMs

### Step 1: Push Changes to VMs

If you're working locally and need to push to VMs:

```bash
# On your local machine (if using git)
git add Makefile src/file_message.cpp src/file_operations_handler.cpp include/file_operations_handler.hpp
git commit -m "Add HyDFS file operations implementation"
git push

# Then on each VM
git pull
```

**OR** if not using git, manually copy these files to your VMs:
- `Makefile` (updated)
- `src/file_message.cpp` (new)
- `src/file_operations_handler.cpp` (new)
- `include/file_operations_handler.hpp` (new)
- `include/file_metadata.hpp` (updated)
- `include/node.hpp` (updated)
- `src/node.cpp` (updated)
- `src/main.cpp` (updated)
- `CMakeLists.txt` (updated, if using cmake)

### Step 2: Clean Build on Each VM

On **each VM**, run:

```bash
cd ~/cs425_mp3  # or wherever your project is
make clean
make -j4
```

You should see:
```
g++ ... -c src/file_message.cpp -o build/file_message.o
g++ ... -c src/file_operations_handler.cpp -o build/file_operations_handler.o
...
g++ ... -o build/main ... build/file_message.o build/file_operations_handler.o ...
```

### Step 3: Verify Build Success

```bash
ls -lh build/main
```

Should show an executable file.

Test it:
```bash
./build/main localhost 12345
```

Should start and print:
```
[timestamp]: Added node: localhost:12345:timestamp in mode: PINGACK
```

Press Ctrl+C to stop, or type `leave` and press Enter.

## Quick Test (3 VMs)

Once built on all VMs:

**VM1 (fa24-cs425-6801.cs.illinois.edu):**
```bash
./build/main localhost 12345
```

**VM2 (fa24-cs425-6802.cs.illinois.edu):**
```bash
./build/main localhost 12345 fa24-cs425-6801.cs.illinois.edu 12345
```
Then type: `join`

**VM3 (fa24-cs425-6803.cs.illinois.edu):**
```bash
./build/main localhost 12345 fa24-cs425-6801.cs.illinois.edu 12345
```
Then type: `join`

### Test File Upload

Create a test file on VM2:
```bash
echo "Hello from VM2!" > test.txt
```

In VM2's running program:
```
create test.txt myfile.txt
ls myfile.txt
```

In VM3's running program:
```
get myfile.txt downloaded.txt
```

Then on VM3's shell:
```bash
cat downloaded.txt
```

Should show: "Hello from VM2!"

## Troubleshooting

### Still Getting Undefined Reference Errors?

Make sure you ran `make clean` before `make`. The old object files may be cached.

```bash
rm -rf build
make -j4
```

### Files Missing?

Ensure all new files are present:
```bash
ls src/file_message.cpp
ls src/file_operations_handler.cpp
ls include/file_operations_handler.hpp
```

If any are missing, copy them from your local machine or the git repository.

### Wrong Compiler?

Check your g++ version:
```bash
g++ --version
```

Need: g++ 7.0 or higher for C++17 support.

### Build Output Doesn't Show New Files?

Your Makefile might not be updated. Check line 26-27:
```bash
sed -n '26,27p' Makefile
```

Should show:
```
            $(SRC_DIR)/file_message.cpp \
            $(SRC_DIR)/file_operations_handler.cpp
```

If not, the Makefile wasn't updated. Copy it again from the fixed version.

## Success!

Once built successfully, you can use all the file commands:
- `create <local> <hydfs>`
- `get <hydfs> <local>`
- `append <local> <hydfs>`
- `merge <hydfs>`
- `ls <hydfs>`
- `store`
- `getfromreplica <vm:port> <hydfs> <local>`

See `HYDFS_USAGE.md` for detailed usage instructions.
