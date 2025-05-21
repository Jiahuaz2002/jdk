#!/bin/bash

RESULT_FILE="benchmark_results.txt"
TMP_RESULT_FILE="tmp_results_unsorted.txt"
PYTHON_SCRIPT="compress.py"
CODE_PARSER="extract_codesize.py"

# Absolute path to compress.py and extract_codesize.py
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON_SCRIPT="$SCRIPT_DIR/compress.py"
CODE_PARSER="$SCRIPT_DIR/extract_codesize.py"

# Clean previous result files
echo "Benchmark Results" > "$TMP_RESULT_FILE"
echo "=================" >> "$TMP_RESULT_FILE"

for i in {1..12}; do
    CLASS="Test$i"
    echo "Running $CLASS..."

    # Run Java test to generate node.txt and edge.txt
    java -cp . -XX:+UnlockDiagnosticVMOptions \
         -XX:CompileCommand=printcompilation,${CLASS}::* \
         -XX:CompileCommand=compileonly,${CLASS}::test \
         -XX:CompileCommand=printinlining,${CLASS}::test \
         -Xbatch \
         -XX:-TieredCompilation \
         -XX:+EnableDeserializeCall \
         "$CLASS" > /dev/null

    # Run compression script
    python3 "$PYTHON_SCRIPT" > tmp_compression_output.txt

    # Parse node.txt summary
    NODE_SUMMARY=$(grep "SUMMARY node.txt" tmp_compression_output.txt)
    NODE_ORIG=$(echo "$NODE_SUMMARY" | awk '{print $3}')
    NODE_METHOD=$(echo "$NODE_SUMMARY" | awk '{print $4}')
    NODE_SIZE=$(echo "$NODE_SUMMARY" | awk '{print $5}')
    if [[ -n "$NODE_SIZE" && -n "$NODE_ORIG" ]]; then
        NODE_RATIO=$(awk "BEGIN {printf \"%.2f\", 100 * $NODE_SIZE / $NODE_ORIG}")
    else
        NODE_RATIO="N/A"
    fi

    # Parse edge.txt summary
    EDGE_SUMMARY=$(grep "SUMMARY edge.txt" tmp_compression_output.txt)
    EDGE_ORIG=$(echo "$EDGE_SUMMARY" | awk '{print $3}')
    EDGE_METHOD=$(echo "$EDGE_SUMMARY" | awk '{print $4}')
    EDGE_SIZE=$(echo "$EDGE_SUMMARY" | awk '{print $5}')
    if [[ -n "$EDGE_SIZE" && -n "$EDGE_ORIG" ]]; then
        EDGE_RATIO=$(awk "BEGIN {printf \"%.2f\", 100 * $EDGE_SIZE / $EDGE_ORIG}")
    else
        EDGE_RATIO="N/A"
    fi

    # Parse structure.txt for node and edge count
    if [[ -f structure.txt ]]; then
        NODE_COUNT=$(head -n1 structure.txt)
        EDGE_COUNT=$(tail -n1 structure.txt)
    else
        NODE_COUNT="?"
        EDGE_COUNT="?"
    fi

    # Run with PrintAssembly to get code size
    ASM_OUTPUT="asm_output_${CLASS}.txt"
    java -cp . -XX:+UnlockDiagnosticVMOptions \
         -XX:+PrintAssembly \
         -XX:CompileCommand=printcompilation,${CLASS}::* \
         -XX:CompileCommand=compileonly,${CLASS}::test \
         -Xbatch \
         -XX:-TieredCompilation \
         -XX:CompileCommand=printinlining,${CLASS}::test \
         -XX:+EnableDeserializeCall \
         "$CLASS" > "$ASM_OUTPUT" 2>&1

    # Use Python to extract correct code size for TestN::test
    CODE_SIZE=$(python3 "$CODE_PARSER" "$ASM_OUTPUT" "${CLASS}::test")
    CODE_SIZE=${CODE_SIZE:-0}

    # Store result with sortable node.txt key
    echo "$NODE_SIZE|$CLASS, node.txt: $NODE_METHOD $NODE_ORIG -> $NODE_SIZE (${NODE_RATIO}%), edge.txt: $EDGE_METHOD $EDGE_ORIG -> $EDGE_SIZE (${EDGE_RATIO}%), structure: $NODE_COUNT nodes, $EDGE_COUNT edges, code size: $CODE_SIZE bytes" >> "$TMP_RESULT_FILE"

    echo "$CLASS done."
done

# Sort by node.txt compressed size and write final results
{
  echo "Benchmark Results (Sorted by node.txt compressed size)"
  echo "======================================================"
  sort -n -t '|' -k1 "$TMP_RESULT_FILE" | cut -d'|' -f2-
} > "$RESULT_FILE"

# Clean up
rm -f tmp_compression_output.txt "$TMP_RESULT_FILE"

