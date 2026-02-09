# Ralph Roadmap

Future features under consideration, ordered roughly by implementation dependency and complexity.

## Model Routing

Switch models dynamically within a conversation based on task requirements. Allow configuration of model aliases and selection rules. Support routing expensive reasoning to capable models while using cheaper models for simple operations.

## Streaming Tool Execution

Execute tools as they stream in from the model rather than waiting for the complete response. Parse partial tool calls and begin execution when all required parameters are available. Reduces end-to-end latency on complex responses.

## Multimodal Input

Support sending images to vision-capable models. Extend message formatting to include base64-encoded image blocks. Handle screenshots, diagrams, and UI captures as conversation input. Requires changes to the API request builder and message schema.
