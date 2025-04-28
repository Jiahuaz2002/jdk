import os
import zlib
import bz2
import lzma
import zstandard as zstd

INPUT_FILE = "node.txt"

def read_input():
    with open(INPUT_FILE, "rb") as f:
        return f.read()

def save_file(filename, data):
    with open(filename, "wb") as f:
        f.write(data)

def compress_all(data):
    results = {}

    # Zlib
    zlib_compressed = zlib.compress(data)
    save_file("node_compressed.zlib", zlib_compressed)
    results["zlib"] = len(zlib_compressed)

    # BZ2
    bz2_compressed = bz2.compress(data)
    save_file("node_compressed.bz2", bz2_compressed)
    results["bz2"] = len(bz2_compressed)

    # LZMA
    lzma_compressed = lzma.compress(data,preset=9)
    save_file("node_compressed.lzma", lzma_compressed)
    results["lzma"] = len(lzma_compressed)

    # Zstandard
    zstd_compressed = zstd.ZstdCompressor().compress(data)
    save_file("node_compressed.zst", zstd_compressed)
    results["zstandard"] = len(zstd_compressed)

    return results

def main():
    data = read_input()
    original_size = len(data)

    print(f"Original size: {original_size} bytes\n")
    results = compress_all(data)

    for method, size in results.items():
        ratio = size / original_size
        print(f"{method:10}: {size:8} bytes  ({ratio:.2%} of original)")

    best = min(results.items(), key=lambda x: x[1])
    print(f"\n🔹 Best compression: {best[0]} ({best[1]} bytes)")

if __name__ == "__main__":
    main()

