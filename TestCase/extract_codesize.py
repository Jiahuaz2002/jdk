import sys
import re

if len(sys.argv) != 3:
    print("Usage: python extract_codesize.py <asm_output_file> <method_name>")
    sys.exit(1)

asm_file = sys.argv[1]
method_name = sys.argv[2]

main_code_size = None
stub_code_size = None

with open(asm_file, "r") as f:
    lines = f.readlines()

# Locate the method block
inside_target = False
for i, line in enumerate(lines):
    if f"Compiled method (c2)" in line and method_name in line:
        inside_target = True
        continue

    if inside_target:
        if "Compiled method (c2)" in line and method_name not in line:
            # Found another method, stop
            break

        if "main code" in line and main_code_size is None:
            match = re.search(r"=\s*(\d+)", line)
            if match:
                main_code_size = int(match.group(1))

        if "stub code" in line and stub_code_size is None:
            match = re.search(r"=\s*(\d+)", line)
            if match:
                stub_code_size = int(match.group(1))

        if main_code_size is not None and stub_code_size is not None:
            break

# Output result
main_code_size = main_code_size or 0
stub_code_size = stub_code_size or 0
total = main_code_size + stub_code_size

print(f"{total}")

