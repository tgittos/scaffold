#ifndef PDF_TOOL_H
#define PDF_TOOL_H

#include "tools_system.h"

int register_pdf_tool(ToolRegistry *registry);
int execute_pdf_extract_text_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif /* PDF_TOOL_H */