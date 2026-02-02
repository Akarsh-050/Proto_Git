# Proto_Git
**A High-Performance Version Control System Core built in C++17**

Proto_Git is a from-scratch implementation of the Git core protocol. It moves beyond simple file storage to implement a **Content-Addressable Storage** system based on **Merkle Trees**. This project explores low-level systems programming, binary data manipulation, and cryptographic integrity.



---

## 🏗 Core Architecture
Proto_Git implements the "Plumbing" layer of Git, transforming a physical directory structure into a series of immutable, compressed, and hashed objects.

* **Blob Storage**: Files are stored as unique blobs. Content-addressing ensures that duplicate files across different versions occupy disk space only once.
* **Tree Objects**: Directories are represented as binary files listing names, modes, and 20-byte SHA-1 pointers to other objects (Blobs or sub-Trees).
* **Commit Objects**: Snapshots of the entire system state, including author metadata, timestamps, and parent-pointers for history tracking.



---

## 🚀 Implemented Commands

| Command | Technical Complexity & Logic |
| :--- | :--- |
| `init` | Standard repository initialization and `.git` structure setup. |
| `hash-object` | The storage pipeline: **Header → SHA-1 → Zlib → Disk Storage**. |
| `cat-file` | Stream decompression and object type verification. |
| `ls-tree` | A binary parser that navigates raw 20-byte hashes in tree buffers. |
| `write-tree` | **Recursive Merkle Tree Construction**: Hashes the entire directory depth-first. |
| `commit-tree` | Links a tree to project history with author metadata and parent-chain pointers. |

---

## 🛠 Tech Stack
* **Language**: C++17
* **Libraries**:
    * `OpenSSL (libcrypto)`: For high-performance SHA-1 Hashing.
    * `Zlib`: For data compression and efficient persistence.
    * `std::filesystem`: For robust cross-platform directory traversal.

---

## ⚙️ Engineering Highlights

### Binary Safety
Unlike standard text-processing applications, **Proto_Git** is built to be **Binary Safe**. It handles data streams containing null bytes (`\0`) and non-printable characters, essential for managing Git's 20-byte raw SHA-1 identifiers and compressed Zlib streams.



### Recursive Determinism
To ensure that the same directory structure always results in the exact same Tree Hash, Proto_Git implements **Lexicographical Sorting** of directory entries before hashing, maintaining 100% compatibility with official Git specifications.

---

## 🏗 Build & Usage

**Prerequisites**: Ensure you have `zlib1g-dev` and `libssl-dev` installed.

**Compile**:
```bash
g++ -std=c++17 src/Main.cpp -o proto_git -lz -lcrypto
