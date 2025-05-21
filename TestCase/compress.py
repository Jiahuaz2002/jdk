import os
import zlib
import bz2
import lzma
import zstandard as zstd

FILES = ["node.txt", "edge.txt"]

def read_input(filename):
    with open(filename, "rb") as f:
        return f.read()

def save_file(filename, data):
    with open(filename, "wb") as f:
        f.write(data)

def compress_all(data, prefix):
    results = {}

    # Zlib
    zlib_compressed = zlib.compress(data)
    save_file(f"{prefix}_compressed.zlib", zlib_compressed)
    results["zlib"] = len(zlib_compressed)

    # BZ2
    bz2_compressed = bz2.compress(data)
    save_file(f"{prefix}_compressed.bz2", bz2_compressed)
    results["bz2"] = len(bz2_compressed)

    # LZMA
    lzma_compressed = lzma.compress(data, preset=9)
    save_file(f"{prefix}_compressed.lzma", lzma_compressed)
    results["lzma"] = len(lzma_compressed)

    # Zstandard
    zstd_compressed = zstd.ZstdCompressor().compress(data)
    save_file(f"{prefix}_compressed.zst", zstd_compressed)
    results["zstandard"] = len(zstd_compressed)

    return results

def process_file(filename):
    data = read_input(filename)
    original_size = len(data)
    prefix = os.path.splitext(filename)[0]

    print(f"\n📄 Processing {filename}")
    print(f"Original size: {original_size} bytes")

    results = compress_all(data, prefix)

    for method, size in results.items():
        ratio = size / original_size
        print(f"{method:10}: {size:8} bytes  ({ratio:.2%} of original)")

    best = min(results.items(), key=lambda x: x[1])
    best_method, best_size = best
    print(f"🔹 Best compression for {filename}: {best_method} ({best_size} bytes)")

    # ADD THIS: for shell script parsing
    print(f"SUMMARY {filename} {original_size} {best_method} {best_size}")

def main():
    for file in FILES:
        if os.path.exists(file):
            process_file(file)
        else:
            print(f"\n⚠️ File not found: {file}")

if __name__ == "__main__":
    main()

