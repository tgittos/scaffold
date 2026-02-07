# Ralph Roadmap

Future features under consideration, ordered roughly by implementation dependency and complexity.

## Tool Call Interruption

Graceful cancellation of long-running tool executions via Ctrl+C. Rather than killing the session, catch the signal, terminate the current operation, and return control to the conversation. Allow the user to redirect or retry.

## Response Caching

Cache results from deterministic tool calls. When the same tool is invoked with identical arguments, return the cached result. Reduces redundant file reads and repeated shell commands. Include cache invalidation based on file modification times.

## Parallel Tool Dispatch

When the model requests multiple independent tools in a single response, execute them concurrently rather than sequentially. Requires dependency analysis to identify which calls are truly independent. Improves latency on multi-tool responses.

## Model Routing

Switch models dynamically within a conversation based on task requirements. Allow configuration of model aliases and selection rules. Support routing expensive reasoning to capable models while using cheaper models for simple operations.

## Streaming Tool Execution

Execute tools as they stream in from the model rather than waiting for the complete response. Parse partial tool calls and begin execution when all required parameters are available. Reduces end-to-end latency on complex responses.

## Multimodal Input

Support sending images to vision-capable models. Extend message formatting to include base64-encoded image blocks. Handle screenshots, diagrams, and UI captures as conversation input. Requires changes to the API request builder and message schema.
