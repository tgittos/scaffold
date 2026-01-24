#!/bin/bash
# Generate embedded_cacert.c from cacert.pem
# Usage: ./scripts/gen_cacert.sh build/cacert.pem src/network/embedded_cacert.c

set -e

INPUT="$1"
OUTPUT="$2"

if [ -z "$INPUT" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: $0 <input.pem> <output.c>" >&2
    exit 1
fi

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found" >&2
    exit 1
fi

SIZE=$(wc -c < "$INPUT" | tr -d ' ')

cat > "$OUTPUT" << 'EOF'
// Auto-generated Mozilla CA certificate bundle
// Source: https://curl.se/ca/cacert.pem
// Regenerate with: make update-cacert

#include "embedded_cacert.h"

const unsigned char embedded_cacert_data[] = {
EOF

# Convert binary to C array
xxd -i < "$INPUT" >> "$OUTPUT"

cat >> "$OUTPUT" << EOF
};

const size_t embedded_cacert_size = $SIZE;
EOF

echo "Generated $OUTPUT ($SIZE bytes)"
