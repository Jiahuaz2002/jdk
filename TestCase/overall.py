import re

input_file = "benchmark_results.txt"
output_file = "benchmark_results_with_columns.txt"

with open(input_file, "r") as f:
    lines = f.readlines()

summary_lines = []
for line in lines:
    if line.startswith("Test") and "node.txt" in line and "code size" in line:
        try:
            # strict and accurate pattern
            node_match = re.search(r"node\.txt: \w+ \d+ -> (\d+)", line)
            edge_match = re.search(r"edge\.txt: \w+ \d+ -> (\d+)", line)
            code_match = re.search(r"code size:\s*(\d+)", line)

            if node_match and edge_match and code_match:
                node_size = int(node_match.group(1))
                edge_size = int(edge_match.group(1))
                code_size = int(code_match.group(1))
                graph_size = node_size + edge_size
                summary_lines.append((graph_size, code_size))
        except Exception:
            continue

with open(output_file, "w") as f:
    f.writelines(lines)
    f.write("\n\nTwo-Column Summary\n")
    f.write("==================\n")
    f.write("Graph Size (bytes)   |   Code Size (bytes)\n")
    f.write("---------------------|---------------------\n")
    for graph_size, code_size in summary_lines:
        f.write(f"{graph_size:>20}   |   {code_size:>17}\n")

