# Ralph Architecture

Ralph is a portable C-based AI assistant that provides a consistent interface across multiple LLM providers with an extensible tools system, persistent memory capabilities, and semantic document processing.

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
    ToolRegistry --> MemoryTool[Memory Tool<br/>memory_tool.c/h]
    ToolRegistry --> PDFTool[PDF Tool<br/>pdf_tool.c/h]
    ToolRegistry --> VectorDBTool[Vector DB Tool<br/>vector_db_tool.c/h]
    
    TodoTool --> TodoManager[Todo Manager<br/>todo_manager.c/h]
    TodoTool --> TodoDisplay[Todo Display<br/>todo_display.c/h]
    
    %% Vector Database System
    VectorDBTool --> VectorDBService[Vector DB Service<br/>vector_db_service.c/h]
    MemoryTool --> VectorDBService
    PDFTool --> VectorDBService
    
    VectorDBService --> VectorDB[Vector Database<br/>vector_db.c/h]
    VectorDB --> HNSWLib[HNSWLIB Library<br/>C++ Wrapper]
    
    %% Embeddings System
    VectorDBTool --> EmbeddingsService[Embeddings Service<br/>embeddings_service.c/h]
    MemoryTool --> EmbeddingsService
    PDFTool --> EmbeddingsService
    
    EmbeddingsService --> Embeddings[Embeddings<br/>embeddings.c/h]
    Embeddings --> HTTPClient
    
    %% Document Processing
    PDFTool --> PDFExtractor[PDF Extractor<br/>pdf_extractor.c/h]
    PDFTool --> DocumentChunker[Document Chunker<br/>document_chunker.c/h]
    
    PDFExtractor --> PDFio[PDFio Library]
    
    %% Utilities
    VectorDBTool --> ToolResultBuilder[Tool Result Builder<br/>tool_result_builder.c/h]
    MemoryTool --> ToolResultBuilder
    PDFTool --> ToolResultBuilder
    
    ToolsSystem --> CommonUtils[Common Utils<br/>common_utils.c/h]
    
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
    VectorDB --> pthread[pthreads]
    VectorDBService --> pthread
    HNSWLib --> CPP[C++ Runtime]
    
    %% File System
    ConversationTracker --> FileSystem[File System<br/>CONVERSATION.md]
    EnvLoader --> EnvFile[.env File]
    PromptLoader --> PromptFile[system.prompt]
    VectorDBService --> VectorStorage[Vector Storage<br/>~/.local/ralph/]
    
    %% Styling
    classDef coreLayer fill:#e1f5fe
    classDef llmLayer fill:#f3e5f5
    classDef toolsLayer fill:#e8f5e8
    classDef sessionLayer fill:#fff3e0
    classDef networkLayer fill:#fce4ec
    classDef utilsLayer fill:#f1f8e9
    classDef vectorLayer fill:#e3f2fd
    classDef externalLayer fill:#efebe9
    
    class CLI,Core,Session,Config coreLayer
    class LLMProvider,ProviderRegistry,Anthropic,OpenAI,LocalAI,ModelCaps,ClaudeModel,GPTModel,QwenModel,DeepSeekModel,DefaultModel llmLayer
    class ToolsSystem,ToolRegistry,FileTools,ShellTool,TodoTool,LinksTool,MemoryTool,PDFTool,VectorDBTool,TodoManager,TodoDisplay toolsLayer
    class ConversationTracker,TokenManager,ConversationCompactor sessionLayer
    class HTTPClient,APICommon networkLayer
    class OutputFormatter,PromptLoader,EnvLoader,JSONUtils,DebugOutput,ToolResultBuilder,CommonUtils utilsLayer
    class VectorDBService,VectorDB,EmbeddingsService,Embeddings,PDFExtractor,DocumentChunker vectorLayer
    class cURL,mbedTLS,FileSystem,EnvFile,PromptFile,VectorStorage,HNSWLib,PDFio,pthread,CPP externalLayer
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
    participant VectorDB as Vector DB Service
    participant Embeddings as Embeddings Service
    participant Network as HTTP Client
    participant LLM as External LLM API
    
    User->>CLI: ralph "remember project uses Redis"
    CLI->>Core: Initialize session
    Core->>Session: Load conversation history
    Core->>VectorDB: Initialize vector services
    Core->>Provider: Detect provider from config
    Core->>Core: Build JSON payload
    Core->>Network: Send HTTP request
    Network->>LLM: API call
    LLM->>Network: Response with tool calls
    Network->>Provider: Parse response
    Provider->>Tools: Execute tool calls (memory_tool)
    Tools->>Embeddings: Generate embeddings
    Embeddings->>Network: Embedding API call
    Network->>Embeddings: Return embeddings
    Tools->>VectorDB: Store in long-term memory
    VectorDB->>Tools: Confirm storage
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
- **Vector Storage**: Pluggable vector database backends with HNSWLIB integration

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
    RalphSession --> VectorServices[Vector Services<br/>Singleton Access]
    
    SessionData --> Config[Configuration]
    SessionData --> CurrentProvider[Current Provider]
    SessionData --> ModelInfo[Model Information]
    
    VectorServices --> VectorDBService[Vector DB Service]
    VectorServices --> EmbeddingsService[Embeddings Service]
```

### 4. Service Layer Pattern
```mermaid
graph TB
    Services[Service Layer<br/>Singleton Management] --> VectorDBSvc[Vector DB Service<br/>Thread-safe singleton]
    Services --> EmbeddingsSvc[Embeddings Service<br/>API abstraction]
    
    VectorDBSvc --> IndexMgmt[Index Management<br/>Multiple indices]
    VectorDBSvc --> VectorOps[Vector Operations<br/>CRUD operations]
    VectorDBSvc --> Search[Similarity Search<br/>K-NN queries]
    
    EmbeddingsSvc --> APICall[API Calls<br/>OpenAI embeddings]
    EmbeddingsSvc --> Caching[Response Caching]
    EmbeddingsSvc --> VectorConversion[Vector Conversion]
```

## Module Dependencies

```mermaid
graph TB
    Core[Core Layer<br/>ralph.c/h] --> LLM[LLM Layer]
    Core --> Tools[Tools Layer]
    Core --> Session[Session Layer]
    Core --> Utils[Utils Layer]
    Core --> Vector[Vector Layer]
    
    LLM --> Network[Network Layer]
    LLM --> Utils
    
    Tools --> Utils
    Tools --> Vector
    
    Session --> Utils
    Session --> Network
    
    Vector --> Network
    Vector --> Utils
    Vector --> Database[Database Layer<br/>HNSWLIB]
    Vector --> Document[Document Processing<br/>PDFio]
    
    Network --> External[External Libraries<br/>cURL, mbedTLS]
    Database --> CPPRuntime[C++ Runtime]
    
    Utils -.-> FileSystem[File System]
    Vector -.-> VectorStorage[Vector Storage<br/>~/.local/ralph/]
    
    classDef layer fill:#f9f9f9,stroke:#333,stroke-width:2px
    classDef vectorLayer fill:#e3f2fd,stroke:#1976d2,stroke-width:2px
    classDef external fill:#ffebee,stroke:#d32f2f,stroke-width:2px
    
    class Core,LLM,Tools,Session,Utils,Network layer
    class Vector vectorLayer
    class External,FileSystem,Database,Document,CPPRuntime,VectorStorage external
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
    ToolRegistry --> MemoryTools[Memory Tools<br/>remember/recall_memories]
    ToolRegistry --> PDFTools[PDF Tools<br/>process_pdf_document]
    ToolRegistry --> VectorDBTools[Vector DB Tools<br/>11 vector operations]
    
    FileTools --> FileSys[File System Operations]
    ShellTool --> Shell[Shell Execution<br/>Security validated]
    TodoTools --> TodoMgr[Todo Manager<br/>In-memory state]
    LinksTools --> EmbeddedLinks[Embedded Links<br/>Binary data]
    
    MemoryTools --> VectorDBSvc[Vector DB Service<br/>Long-term memory index]
    MemoryTools --> EmbeddingsSvc[Embeddings Service<br/>Text to vectors]
    
    PDFTools --> PDFExtractor[PDF Extractor<br/>PDFio library]
    PDFTools --> DocumentChunker[Document Chunker<br/>Intelligent segmentation]
    PDFTools --> VectorDBSvc
    PDFTools --> EmbeddingsSvc
    
    VectorDBTools --> VectorDBSvc
    VectorDBTools --> EmbeddingsSvc
    VectorDBTools --> IndexMgmt[Index Management<br/>Create/delete/list]
    VectorDBTools --> VectorOps[Vector Operations<br/>CRUD + search]
    
    ToolSystem --> ToolResultBuilder[Tool Result Builder<br/>Standardized responses]
    ToolResultBuilder --> ToolResult[Tool Result<br/>JSON Response]
    
    classDef toolCore fill:#e8f5e8
    classDef toolImpl fill:#c8e6c9
    classDef vectorTools fill:#e3f2fd
    classDef external fill:#ffcdd2
    
    class ToolCall,ToolSystem,ToolRegistry,ToolResult,ToolResultBuilder toolCore
    class FileTools,ShellTool,TodoTools,LinksTools,TodoMgr toolImpl
    class MemoryTools,PDFTools,VectorDBTools,VectorDBSvc,EmbeddingsSvc,PDFExtractor,DocumentChunker,IndexMgmt,VectorOps vectorTools
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
- **Vector Database Integration**: Persistent semantic memory with HNSWLIB backend
- **Document Processing**: Automatic PDF extraction, chunking, and indexing
- **Embeddings Service**: Centralized embedding generation with OpenAI's text-embedding-3-small
- **Long-term Memory System**: Semantic storage and retrieval of important information
- **Thread-Safe Services**: Concurrent access to vector databases with mutex protection
- **Portable**: Built with Cosmopolitan for universal binary compatibility
- **Memory Safe**: Defensive programming with comprehensive error handling
- **Testable**: Extensive test suite covering all major components including vector operations

## New Tool Categories

### Memory Tools
- **`remember`**: Store information in long-term semantic memory
- **`recall_memories`**: Search and retrieve relevant memories using semantic similarity

### PDF Processing Tools  
- **`process_pdf_document`**: Extract text from PDFs and automatically index in vector database

### Vector Database Tools (11 tools)
- **Index Management**: `vector_db_create_index`, `vector_db_delete_index`, `vector_db_list_indices`
- **Vector Operations**: `vector_db_add_vector`, `vector_db_update_vector`, `vector_db_delete_vector`, `vector_db_get_vector`
- **Search Operations**: `vector_db_search`
- **Text Operations**: `vector_db_add_text`, `vector_db_add_chunked_text`, `vector_db_add_pdf_document`

## Storage and Persistence

- **Default Storage Location**: `~/.local/ralph/` for all vector database indices
- **Index Configuration**: Separate configurations for memory storage vs document storage
- **Thread Safety**: Mutex-protected singleton services for concurrent access
- **Automatic Initialization**: Services initialize automatically on first use
- **Rich Metadata**: Timestamps, content types, and classification for all stored vectors