# Ralph Architecture

Ralph is a portable C-based AI assistant that provides a consistent interface across multiple LLM providers with an extensible tools system.

## System Architecture

```mermaid
graph TB
    %% User Interface Layer
    CLI[Command Line Interface<br/>main.c] --> Core[Ralph Core<br/>ralph.c/h]
    
    %% Core Application Layer
    Core --> Session[Session Manager<br/>session_manager.c/h]
    Core --> Config[Configuration<br/>RalphConfig]
    
    %% LLM Integration Layer
    Core --> LLMProvider[LLM Provider Abstraction<br/>llm_provider.c/h]
    LLMProvider --> ProviderRegistry[Provider Registry]
    
    ProviderRegistry --> Anthropic[Anthropic Provider<br/>anthropic_provider.c]
    ProviderRegistry --> OpenAI[OpenAI Provider<br/>openai_provider.c]
    ProviderRegistry --> LocalAI[Local AI Provider<br/>local_ai_provider.c]
    
    %% Model Capabilities
    LLMProvider --> ModelCaps[Model Capabilities<br/>model_capabilities.c/h]
    ModelCaps --> ClaudeModel[Claude Model<br/>claude_model.c]
    ModelCaps --> GPTModel[GPT Model<br/>gpt_model.c]
    ModelCaps --> QwenModel[Qwen Model<br/>qwen_model.c]
    ModelCaps --> DeepSeekModel[DeepSeek Model<br/>deepseek_model.c]
    ModelCaps --> DefaultModel[Default Model<br/>default_model.c]
    
    %% Tools System
    Core --> ToolsSystem[Tools System<br/>tools_system.c/h]
    ToolsSystem --> ToolRegistry[Tool Registry]
    
    ToolRegistry --> FileTools[File Tools<br/>file_tools.c/h]
    ToolRegistry --> ShellTool[Shell Tool<br/>shell_tool.c/h]
    ToolRegistry --> TodoTool[Todo Tool<br/>todo_tool.c/h]
    ToolRegistry --> LinksTool[Links Tool<br/>links_tool.c/h]
    
    TodoTool --> TodoManager[Todo Manager<br/>todo_manager.c/h]
    TodoTool --> TodoDisplay[Todo Display<br/>todo_display.c/h]
    
    %% Session Management Layer
    Session --> ConversationTracker[Conversation Tracker<br/>conversation_tracker.c/h]
    Session --> TokenManager[Token Manager<br/>token_manager.c/h]
    Session --> ConversationCompactor[Conversation Compactor<br/>conversation_compactor.c/h]
    
    %% Network Layer
    Anthropic --> HTTPClient[HTTP Client<br/>http_client.c/h]
    OpenAI --> HTTPClient
    LocalAI --> HTTPClient
    
    HTTPClient --> APICommon[API Common<br/>api_common.c/h]
    
    %% Utilities Layer
    Core --> OutputFormatter[Output Formatter<br/>output_formatter.c/h]
    Core --> PromptLoader[Prompt Loader<br/>prompt_loader.c/h]
    Core --> EnvLoader[Env Loader<br/>env_loader.c/h]
    
    ToolsSystem --> JSONUtils[JSON Utils<br/>json_utils.c/h]
    APICommon --> JSONUtils
    ConversationTracker --> JSONUtils
    
    Core --> DebugOutput[Debug Output<br/>debug_output.c/h]
    
    %% External Dependencies
    HTTPClient --> cURL[cURL Library]
    HTTPClient --> mbedTLS[mbedTLS Library]
    
    %% File System
    ConversationTracker --> FileSystem[File System<br/>CONVERSATION.md]
    EnvLoader --> EnvFile[.env File]
    PromptLoader --> PromptFile[system.prompt]
    
    %% Styling
    classDef coreLayer fill:#e1f5fe
    classDef llmLayer fill:#f3e5f5
    classDef toolsLayer fill:#e8f5e8
    classDef sessionLayer fill:#fff3e0
    classDef networkLayer fill:#fce4ec
    classDef utilsLayer fill:#f1f8e9
    classDef externalLayer fill:#efebe9
    
    class CLI,Core,Session,Config coreLayer
    class LLMProvider,ProviderRegistry,Anthropic,OpenAI,LocalAI,ModelCaps,ClaudeModel,GPTModel,QwenModel,DeepSeekModel,DefaultModel llmLayer
    class ToolsSystem,ToolRegistry,FileTools,ShellTool,TodoTool,LinksTool,TodoManager,TodoDisplay toolsLayer
    class ConversationTracker,TokenManager,ConversationCompactor sessionLayer
    class HTTPClient,APICommon networkLayer
    class OutputFormatter,PromptLoader,EnvLoader,JSONUtils,DebugOutput utilsLayer
    class cURL,mbedTLS,FileSystem,EnvFile,PromptFile externalLayer
```

## Component Data Flow

```mermaid
sequenceDiagram
    participant User
    participant CLI as main.c
    participant Core as ralph.c
    participant Session as Session Manager
    participant Provider as LLM Provider
    participant Tools as Tools System
    participant Network as HTTP Client
    participant LLM as External LLM API
    
    User->>CLI: ralph "message"
    CLI->>Core: Initialize session
    Core->>Session: Load conversation history
    Core->>Provider: Detect provider from config
    Core->>Core: Build JSON payload
    Core->>Network: Send HTTP request
    Network->>LLM: API call
    LLM->>Network: Response with tool calls
    Network->>Provider: Parse response
    Provider->>Tools: Execute tool calls
    Tools->>Tools: Run shell/file operations
    Tools->>Provider: Return tool results
    Provider->>Core: Format final response
    Core->>Session: Save conversation
    Core->>CLI: Display formatted output
    CLI->>User: Show response
```

## Key Architectural Patterns

### 1. Plugin Architecture
- **LLM Providers**: Each provider implements the `LLMProvider` interface
- **Tools**: Each tool registers with the `ToolRegistry` for dynamic execution
- **Models**: Model-specific capabilities are registered in the `ModelRegistry`

### 2. Registry Pattern
```mermaid
graph LR
    Registry[Registry Pattern] --> PR[Provider Registry<br/>Dynamic provider detection]
    Registry --> TR[Tool Registry<br/>Dynamic tool execution]
    Registry --> MR[Model Registry<br/>Model capability management]
    
    PR --> Detection[URL-based detection]
    TR --> Execution[JSON-based tool calls]
    MR --> Capabilities[Context windows, features]
```

### 3. Session-Based Design
```mermaid
graph TB
    RalphSession[Ralph Session<br/>Central Context] --> SessionData[Session Data<br/>Core State]
    RalphSession --> ConvHistory[Conversation History]
    RalphSession --> ToolReg[Tool Registry]
    RalphSession --> ProvReg[Provider Registry]
    RalphSession --> TokenMgmt[Token Management]
    
    SessionData --> Config[Configuration]
    SessionData --> CurrentProvider[Current Provider]
    SessionData --> ModelInfo[Model Information]
```

## Module Dependencies

```mermaid
graph TB
    Core[Core Layer<br/>ralph.c/h] --> LLM[LLM Layer]
    Core --> Tools[Tools Layer]
    Core --> Session[Session Layer]
    Core --> Utils[Utils Layer]
    
    LLM --> Network[Network Layer]
    LLM --> Utils
    
    Tools --> Utils
    
    Session --> Utils
    Session --> Network
    
    Network --> External[External Libraries<br/>cURL, mbedTLS]
    
    Utils -.-> FileSystem[File System]
    
    classDef layer fill:#f9f9f9,stroke:#333,stroke-width:2px
    classDef external fill:#ffebee,stroke:#d32f2f,stroke-width:2px
    
    class Core,LLM,Tools,Session,Utils,Network layer
    class External,FileSystem external
```

## Tool System Architecture

```mermaid
graph TB
    ToolCall[Tool Call Request<br/>JSON] --> ToolSystem[Tools System<br/>tools_system.c]
    ToolSystem --> ToolRegistry[Tool Registry<br/>Dynamic Lookup]
    
    ToolRegistry --> FileTools[File Tools<br/>read/write/search/list]
    ToolRegistry --> ShellTool[Shell Tool<br/>Command execution]
    ToolRegistry --> TodoTools[Todo Tools<br/>Task management]
    ToolRegistry --> LinksTools[Links Tool<br/>URL management]
    
    FileTools --> FileSys[File System Operations]
    ShellTool --> Shell[Shell Execution<br/>Security validated]
    TodoTools --> TodoMgr[Todo Manager<br/>In-memory state]
    LinksTools --> EmbeddedLinks[Embedded Links<br/>Binary data]
    
    ToolSystem --> ToolResult[Tool Result<br/>JSON Response]
    
    classDef toolCore fill:#e8f5e8
    classDef toolImpl fill:#c8e6c9
    classDef external fill:#ffcdd2
    
    class ToolCall,ToolSystem,ToolRegistry,ToolResult toolCore
    class FileTools,ShellTool,TodoTools,LinksTools,TodoMgr toolImpl
    class FileSys,Shell,EmbeddedLinks external
```

## Provider Abstraction

```mermaid
graph TB
    APICall[API Request] --> LLMProvider[LLM Provider Interface]
    
    LLMProvider --> Detection{Provider Detection<br/>URL-based}
    Detection -->|api.anthropic.com| Anthropic[Anthropic Provider]
    Detection -->|api.openai.com| OpenAI[OpenAI Provider]
    Detection -->|localhost:1234| LocalAI[Local AI Provider]
    
    Anthropic --> AnthropicAPI[Anthropic API<br/>Messages format]
    OpenAI --> OpenAIAPI[OpenAI API<br/>Chat completions]
    LocalAI --> LocalAPI[LM Studio API<br/>Compatible format]
    
    AnthropicAPI --> ModelCaps[Model Capabilities]
    OpenAIAPI --> ModelCaps
    LocalAPI --> ModelCaps
    
    ModelCaps --> Features[Features:<br/>• Context windows<br/>• Tool calling<br/>• Thinking tags<br/>• JSON mode]
    
    classDef interface fill:#e3f2fd
    classDef provider fill:#f3e5f5
    classDef api fill:#fff3e0
    classDef capabilities fill:#e8f5e8
    
    class APICall,LLMProvider,Detection interface
    class Anthropic,OpenAI,LocalAI provider
    class AnthropicAPI,OpenAIAPI,LocalAPI api
    class ModelCaps,Features capabilities
```

## Key Features

- **Multi-Provider Support**: Seamlessly works with Anthropic, OpenAI, and local LLM servers
- **Extensible Tools**: Plugin architecture for adding new tools and capabilities
- **Conversation Persistence**: Automatic conversation tracking and history management
- **Token Management**: Intelligent context window optimization and conversation compaction
- **Portable**: Built with Cosmopolitan for universal binary compatibility
- **Memory Safe**: Defensive programming with comprehensive error handling
- **Testable**: Extensive test suite covering all major components