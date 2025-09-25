# MiniVSFS

A lightweight educational implementation of a **Virtual Simple File System (VSFS)**.  
This project demonstrates how file systems manage storage, inodes, directories, and files inside a simulated disk image.

---

## Table of Contents
- [About](#about)
- [Features](#features)
- [File System Structure](#file-system-structure)
- [Installation](#installation)
- [Usage](#usage)
- [Technical Specifications](#technical-specifications)
- [Project Structure](#project-structure)
- [Limitations](#limitations)
- [Error Handling](#error-handling)
---

## About
The **MiniVSFS** project is a simplified file system built for educational purposes.  
It allows users to:
- Create and manage virtual disk images.
- Add, read, and manage files inside the simulated environment.
- Understand low-level storage concepts such as blocks, inodes, and directories.

This project is ideal for students learning **Operating Systems**, **File System Design**, or **Systems Programming**.

---

## Features
- Block-based storage (4KB blocks)
- Inode-based file system (128-byte inodes)
- Direct block addressing (up to 12 blocks per file)
- Directory entries (64 bytes each)
- Bitmap allocation for inodes and data blocks
- CRC32 checksums for metadata integrity
- Root directory with standard `.` and `..` entries
- Command-line utilities to create and modify the filesystem

---

## File System Structure

| Section        | Block(s)       | Description                        |
|----------------|---------------|------------------------------------|
| Superblock     | 0             | Filesystem metadata, CRC32 checksum|
| Inode Bitmap   | 1             | Tracks allocated inodes            |
| Data Bitmap    | 2             | Tracks allocated data blocks       |
| Inode Table    | 3 to 3 + N    | Stores inode structures            |
| Data Region    | Remaining     | Actual file contents               |


## Usage

Creating a New Filesystem
./mkfs_builder --image filesystem.img --size-kib 1024 --inodes 256
Parameters:
--image: Output image filename
--size-kib: Total size in KB (multiple of 4, 180–4096)
--inodes: Number of inodes (128–512)

Adding Files to the Filesystem
./mkfs_adder --input filesystem.img --output filesystem_new.img --file myfile.txt
Parameters:
--input: Input filesystem image
--output: Output filesystem image
--file: File to add

Inspecting Disk Image
xxd -l 512 filesystem_new.img | less
Dumps the first 512 bytes (superblock area) of the image.

---

## Technical Specifications

Superblock (116 bytes)
Magic number: 0x4D565346
Version, block size, total blocks
Bitmap and inode table locations
Data region layout
CRC32 checksum

Inode (128 bytes)
File mode and permissions
Size, timestamps (atime, mtime, ctime)
Direct block pointers (12 blocks max)
User/group IDs
CRC32 checksum

Directory Entry (64 bytes)
Inode number
File type (file/directory)
Filename (up to 57 characters)
XOR checksum

---
## Project Structure

```
├── mkfs_builder.c    # Filesystem creation utility
├── mkfs_adder.c      # File addition utility
├── file_*.txt        # Sample test files
└── README.md         # This documentation
```


## Limitations

- Maximum file size: 48 KB (12 × 4KB blocks)
- Only root directory supported (no subdirectories)
- No symbolic links or extended attributes
- Maximum entries per directory block

---

## Error Handling

The implementation includes comprehensive error checking for:

- Invalid filesystem images
- Insufficient space (inodes/data blocks)
- File size limitations
- Duplicate filenames
- I/O errors

## Installation

1. Clone this repository:
https://github.com/TahsinTanni/MiniVSFS.git

2. cd MiniVSFS

3. Compile the utilities:
gcc -o mkfs_builder mkfs_builder.c
gcc -o mkfs_adder mkfs_adder.c

4. Make sure the binaries are executable:
chmod +x mkfs_builder mkfs_adder

## Helper DOC

You can view the helper documentation [here](https://docs.google.com/document/d/1TKeNfHOCvLOceYXpUFmnTPE4n6Zna1RU2-40olV_XSs/edit?tab=t.2t18mw9qpgw1).

