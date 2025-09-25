# MiniVSFS

A lightweight educational implementation of a **Virtual Simple File System (VSFS)**.  
This project demonstrates how file systems manage storage, inodes, directories, and files inside a simulated disk image.

---

## Table of Contents
- [About](#about)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Contributors](#contributors)
- [License](#license)
- [Contact](#contact)

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
- Initialize a disk image with a custom size and inode count.
- Add files into the virtual file system.
- Read file metadata and contents.
- Inspect superblock and directory entries.
- Command-line utilities to interact with the file system.

---

## Installation
1. Clone this repository:
   ```bash
   git clone https://github.com/<your-username>/MiniVSFS.git
   cd MiniVSFS
