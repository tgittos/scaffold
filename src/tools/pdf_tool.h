#ifndef PDF_TOOL_H
#define PDF_TOOL_H

#include "tools_system.h"

// Register the PDF text extraction tool with the tool registry
int register_pdf_tool(ToolRegistry *registry);

// Execute a PDF tool call to extract text from a PDF file
int execute_pdf_extract_text_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif /* PDF_TOOL_H */