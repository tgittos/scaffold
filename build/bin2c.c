#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_binary> <var_name>\n", argv[0]);
        return 1;
    }
    
    FILE *input = fopen(argv[1], "rb");
    if (!input) {
        perror("Failed to open input file");
        return 1;
    }
    
    FILE *output = stdout;  // Output to stdout for redirection
    
    // Get file size
    fseek(input, 0, SEEK_END);
    long size = ftell(input);
    fseek(input, 0, SEEK_SET);
    
    fprintf(output, "// Auto-generated from %s\n", argv[1]);
    fprintf(output, "#ifndef EMBEDDED_LINKS_H\n");
    fprintf(output, "#define EMBEDDED_LINKS_H\n\n");
    fprintf(output, "#include <stddef.h>\n\n");
    fprintf(output, "static const unsigned char %s_data[] = {\n    ", argv[2]);
    
    int byte;
    int count = 0;
    while ((byte = fgetc(input)) != EOF) {
        fprintf(output, "0x%02x,", byte);
        count++;
        if (count % 16 == 0) {
            fprintf(output, "\n    ");
        } else {
            fprintf(output, " ");
        }
    }
    
    fprintf(output, "\n};\n\n");
    fprintf(output, "static const size_t %s_size = %ld;\n\n", argv[2], size);
    fprintf(output, "#endif // EMBEDDED_LINKS_H\n");
    
    fclose(input);
    
    fprintf(stderr, "Generated %s (%ld bytes)\n", argv[2], size);
    return 0;
}