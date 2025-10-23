#!/bin/bash

# Simple test script for HyDFS file operations
# This creates test files and provides example commands

echo "=== HyDFS Test Setup ==="

# Create test directory
mkdir -p test_files
cd test_files

# Create test files
echo "Creating test files..."
echo "Hello, this is test file 1!" > test1.txt
echo "This is test file 2 with different content." > test2.txt
echo "Third test file content here." > test3.txt
echo "Data to append" > append1.txt
echo "More data to append" > append2.txt

echo "Test files created in test_files/ directory:"
ls -la

echo ""
echo "=== Quick Start Guide ==="
echo ""
echo "1. Start the introducer (in terminal 1):"
echo "   ./build/main localhost 12345"
echo ""
echo "2. Start VM2 (in terminal 2):"
echo "   ./build/main localhost 12346 localhost 12345"
echo "   Then type: join"
echo ""
echo "3. Start VM3 (in terminal 3):"
echo "   ./build/main localhost 12347 localhost 12345"
echo "   Then type: join"
echo ""
echo "4. Test file operations:"
echo "   In VM2 terminal:"
echo "     create test_files/test1.txt file1.txt"
echo "     ls file1.txt"
echo "     store"
echo ""
echo "   In VM3 terminal:"
echo "     get file1.txt retrieved.txt"
echo "     append test_files/append1.txt file1.txt"
echo ""
echo "5. Verify in shell:"
echo "   cat retrieved.txt"
echo ""
echo "=== Available Commands ==="
echo "  create <localfile> <hydfsfile>   - Upload file to HyDFS"
echo "  get <hydfsfile> <localfile>      - Download file from HyDFS"
echo "  append <localfile> <hydfsfile>   - Append to existing file"
echo "  merge <hydfsfile>                - Merge all replicas"
echo "  ls <hydfsfile>                   - List file locations"
echo "  store                            - List local files"
echo "  getfromreplica <vm:port> <hydfsfile> <localfile>"
echo ""
echo "=== Membership Commands ==="
echo "  join                - Join the network"
echo "  leave               - Leave the network"
echo "  list_mem            - List all members"
echo "  list_mem_ids        - List members with ring IDs"
echo "  list_self           - Show self info"
echo ""
