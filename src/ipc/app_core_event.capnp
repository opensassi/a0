@0xd4a7f2c1b3e809f7;

struct AppCoreEvent {
    union {
        # --- Commands (TUI → AppCore) ---
        submitGoal    @0  : SubmitGoal;
        cancel        @1  : Void;
        shutdown      @2  : Void;
        setSession    @3  : SetSession;
        listSessions  @4  : ListSessions;
        resumeSession @5  : ResumeSession;
        loadResource  @6  : LoadResource;

        # --- Events (AppCore → TUI) ---
        llmStart        @7  : LlmStart;
        llmChunk        @8  : LlmChunk;
        llmComplete     @9  : LlmComplete;
        toolStart       @10 : ToolStart;
        toolChunk       @11 : ToolChunk;
        toolEnd         @12 : ToolEnd;
        complete        @13 : Complete;
        error           @14 : Error;
        sessionReady    @15 : SessionReady;
        sessionList     @16 : SessionList;
        sessionHistory  @17 : SessionHistory;
        loadResourceResult @18 : LoadResourceResult;

        # --- Control (c2 → b1) ---
        followAgent     @19 : FollowAgent;
        unfollowAgent   @20 : UnfollowAgent;
    }
}

struct SubmitGoal    { goal @0 :Text; }
struct SetSession    { sessionDbId @0 :Int64; sessionUuid @1 :Text; }
struct ListSessions  { limit @0 :Int32; }
struct ResumeSession { uuid @0 :Text; }
struct LoadResource  { type @0 :ResourceType; id @1 :Int64; offset @2 :Int64; limit @3 :Int64; }

struct LlmStart      { streamId @0 :Int64; roundSeq @1 :Int32; }
struct LlmChunk      { streamId @0 :Int64; seq @1 :Int32; text @2 :Text; isFinal @3 :Bool; }
struct LlmComplete   { streamId @0 :Int64; finishReason @1 :Text; }
struct ToolStart     { invocationId @0 :Int64; toolCallId @1 :Text; toolName @2 :Text; arguments @3 :Text; }
struct ToolChunk     { invocationId @0 :Int64; seq @1 :Int32; text @2 :Text; streamType @3 :Text; }
struct ToolEnd       { invocationId @0 :Int64; exitCode @1 :Int32; totalBytes @2 :Int64; outputPreview @3 :Text; }
struct Complete      { sessionId @0 :Int64; summary @1 :Text; }
struct Error         { source @0 :Text; contextId @1 :Int64; message @2 :Text; }
struct SessionReady  { dbId @0 :Int64; uuid @1 :Text; }
struct SessionList   { entries @0 :List(Entry); }
struct SessionHistory { dbId @0 :Int64; uuid @1 :Text; found @2 :Bool; messages @3 :List(SessionMessage); }
struct LoadResourceResult { id @0 :Int64; data @1 :Text; }
struct FollowAgent   { sessionUuid @0 :Text; }
struct UnfollowAgent { sessionUuid @0 :Text; }

struct Entry {
    uuid @0 :Text;
    dbId @1 :Int64;
    startedAt @2 :Text;
    messageCount @3 :Int32;
}

struct SessionMessage {
    role @0 :Text;
    content @1 :Text;
    toolCallId @2 :Text;
    name @3 :Text;
    resultJson @4 :Text;
    createdAt @5 :Int64;
}

enum ResourceType {
    llmStream @0;
    toolOutput @1;
    terminalStream @2;
    toolInvocation @3;
}
