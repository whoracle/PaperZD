# Live state-machine debugging in PIE and Simulate

## Goal

When a PaperZD Animation Blueprint is open while Play In Editor (PIE) or Simulate In Editor (SIE) is running, its state-machine graph should visualize the selected runtime `UPaperZDAnimInstance`:

- the current state is highlighted and shows how long it has been active;
- transitions taken recently are highlighted long enough to be visible;
- a transition whose transitional animation graph is still playing remains visibly active;
- conduit paths and multiple transitions taken in one update are represented correctly;
- changing the Blueprint debug-object dropdown immediately switches the displayed instance;
- stopping PIE/SIE or clearing the debug object restores the normal graph appearance.

This should follow Unreal's existing Animation Blueprint debugging model: the toolbar selects an object to debug, generated-class metadata maps editor nodes to compiled indices, and graph Slate widgets read a small runtime snapshot from that object. It should not create a second PaperZD-specific instance selector.

## Current state

The repository already contains most of the integration points, but no data connects them:

- `FPaperZDAnimBPDebugData` exists in `PaperZDAnimBPGeneratedClass.h`, but is empty and is not stored on `UPaperZDAnimBPGeneratedClass`.
- `UPaperZDAnimGraphNode_StateMachine::OnProcessDuringCompilation` already assigns stable indices to baked machines, states, rules, and links, but discards the editor-node-to-index relationship after compilation.
- `FPaperZDAnimNode_StateMachine` already owns `CurrentStateIndex`, `CurrentStateTime`, and `CurrentTransitionalAnimNode` at runtime. It does not retain which transition was taken.
- `SPaperZDStateGraphNode_State` has TODOs for active-state coloring and popup text.
- `SPaperZDStateGraphNode_Transition::StaticGetTransitionColor` has a TODO for active-transition coloring, and the connection drawing policy already calls it for transition wires.
- `FPaperZDAnimBPEditor` derives from `FBlueprintEditor`, so the standard Blueprint debug-object plumbing and `UPaperZDAnimBP::GetObjectBeingDebugged()` are available. Its commented `HandleSetObjectBeingDebugged` declaration shows the intended extension point if an explicit refresh is required.

## Proposed data flow

```text
editor node GUIDs
    | compile
    v
generated-class debug metadata
    | machine/state/transition indices
    v
selected PIE/SIE UPaperZDAnimInstance
    | runtime state-machine snapshot
    v
state and transition Slate widgets + connection drawing policy
```

Use GUIDs in generated metadata rather than pointers to editor graph nodes. GUIDs survive graph duplication/compilation boundaries, avoid editor-module types in the runtime module, and make stale mappings easy to reject after recompilation.

## Implementation plan

### 1. Define generated-class debug metadata

Extend `Source/PaperZD/Public/PaperZDAnimBPGeneratedClass.h` with editor-only debug records. A practical layout is:

- `FPaperZDAnimStateMachineDebugData`
  - state-machine graph/node GUID;
  - baked `StateMachineIndex`;
  - `TMap<FGuid, int32> StateNodeToIndex` for state and conduit nodes;
  - `TMap<FGuid, int32> TransitionNodeToIndex` for explicit transition nodes.
- `FPaperZDAnimBPDebugData`
  - `TMap<FGuid, FPaperZDAnimStateMachineDebugData> StateMachines`, keyed by the state-machine graph or owning state-machine node GUID;
  - optional animation-node GUID-to-property-index mapping, if parent AnimGraph highlighting is included in this change.

Store one `FPaperZDAnimBPDebugData` on `UPaperZDAnimBPGeneratedClass` under `WITH_EDITORONLY_DATA`, expose const and compiler-only mutable accessors, and clear it in `PurgeClass`. Add the mutable accessor to `FPaperZDAnimBPGeneratedClassAccess`.

The debug transition index must identify an explicit graph edge, not merely a target state. Add an editor-only `DebugTransitionIndex` to each baked `FPaperZDAnimStateMachineLink`. This handles parallel transitions between the same pair of states and keeps visualization independent of transition-rule function names.

All new reflected fields need safe defaults (`INDEX_NONE`, invalid GUID) and must tolerate assets compiled before the metadata exists.

### 2. Populate mappings during compilation

Update `UPaperZDAnimGraphNode_StateMachine::OnProcessDuringCompilation`:

1. Create the machine debug record immediately after allocating `StateMachineIndex`.
2. When `GraphNodeToStateMachineNodeId` receives a state or conduit, also record `Node->NodeGuid -> BakedNodeIndex`.
3. When baking each explicit transition, allocate its debug transition index, write it to the baked link, and record `GraphTransition->NodeGuid -> DebugTransitionIndex`.
4. Preserve mappings for every link in a conduit path; do not collapse the visual identity to the final state-to-state link.
5. Emit a compiler warning, rather than crashing the editor, if a source node has an invalid GUID or a mapping cannot be completed.

Reset the generated debug data at the start of every compile so deleted nodes and changed priorities cannot leave stale entries. Use the compiler message log's source-object mapping when compilation operates on cloned/consolidated graph nodes, as the existing commented debug-data block in `PaperZDAnimBPCompilerContext.cpp` already anticipates.

Add validation assertions in non-shipping builds:

- every mapped machine index exists;
- every mapped state index exists in that machine;
- every explicit transition has exactly one valid debug transition index;
- parallel edges receive different transition indices.

### 3. Capture a runtime debug snapshot

Add an editor-only snapshot to `FPaperZDAnimNode_StateMachine`, with read-only accessors for the editor:

- current state index (the existing `CurrentStateIndex` can be exposed through a getter);
- current state elapsed time (`CurrentStateTime`);
- the debug indices of all transitions taken in the most recent transition chain;
- age in seconds of that recent chain;
- the transition index whose transitional animation graph is currently playing, or `INDEX_NONE`.

Do not expose mutable runtime internals or raw link pointers to Slate.

Instrument `OnUpdate`, `SetState`, and transition evaluation so that:

- a newly taken transition chain replaces the previous recent chain and resets its age to zero;
- the chain includes each explicit edge traversed through conduits, in order;
- the recent chain remains available for a short configurable visualization window (start with `0.20s`) so an instantaneous transition is not missed between Slate paints;
- the owning transition stays active for the full lifetime of `CurrentTransitionalAnimNode`, even after the recent-transition pulse expires;
- `JumpToNode` updates the current state but does not fabricate a transition edge; an optional state popup can say that the state was entered by a jump;
- reinitialization and invalid machine data clear the snapshot.

`CheckValidTransition` currently returns only the terminal link after recursively evaluating conduits. Refactor its internal result to carry the accepted link path (or append accepted debug indices to the evaluation context only after the whole path succeeds). Do not record failed rules or speculative conduit paths.

Keep this instrumentation behind `WITH_EDITORONLY_DATA`/`WITH_EDITOR` so packaged non-editor builds do not retain visualization arrays or pay per-tick tracing cost. PaperZD currently updates these nodes on the game thread; document that the accessors are game-thread-only. If node updates move to worker threads later, copy the snapshot to a game-thread-owned buffer before Slate reads it.

### 4. Resolve the selected PIE/SIE instance

Add a small editor-side helper, for example in `Editors/Util/PaperZDAnimBPDebugUtils.{h,cpp}`, to centralize all lookup logic:

1. Find the owning `UPaperZDAnimBP` with `FBlueprintEditorUtils::FindBlueprintForNode`.
2. Read `AnimBP->GetObjectBeingDebugged()`.
3. Cast it to `UPaperZDAnimInstance` and its class to `UPaperZDAnimBPGeneratedClass`.
4. Reject pending-kill objects, class mismatches, stale generated classes, and absent debug metadata.
5. Find the runtime `FPaperZDAnimNode_StateMachine` whose `StateMachineIndex` matches the graph's compiled machine record.
6. Return an immutable view/snapshot to the caller.

Use the generic Blueprint debug dropdown as the source of truth. Verify that PaperZD instances appear in both PIE and SIE. If generic discovery does not populate them, implement the smallest necessary `FPaperZDAnimBPEditor` override of `HandleSetObjectBeingDebugged` and/or debug-object filtering; do not add a parallel toolbar control.

On a debug-object change, Blueprint recompilation, EndPIE, or EndSIE, invalidate cached lookups and request graph refresh/repaint. Prefer weak object pointers and recompute mappings cheaply rather than holding a PIE object alive.

### 5. Render live state information

Implement the existing TODOs in the Slate graph nodes.

For `SPaperZDStateGraphNode_State`:

- return the existing inactive color when there is no valid debug object or mapping;
- use a bright accent color for the current state;
- optionally use a dim accent for states that were part of the recent transition chain, if source/target indices are retained;
- add a node info popup such as `Active for 1.24 s`;
- keep selection, hover, error, and read-only visuals legible over the debug tint.

For `SPaperZDStateGraphNode_Transition` and `FPaperZDStateMachineConnectionDrawingPolicy`:

- use the normal authored transition color while inactive;
- pulse/fade from an accent color to the normal color over the recent-transition window;
- keep the accent color steady while its transitional animation graph is playing;
- increase active wire thickness slightly and enable bubbles/direction motion if supported consistently by all target UE versions;
- preserve hover deemphasis and make hover take precedence without hiding active status;
- add a popup/tooltip status (`Transition taken`, `Transitional animation playing`) where useful.

The transition node body and its wire must query the same helper so they cannot disagree. Slate bindings should query current state on paint/tick; they should not require rebuilding graph widgets every frame.

Suggested initial visual states:

| Runtime status | State node | Transition node/wire |
| --- | --- | --- |
| No debug object | Existing appearance | Existing appearance |
| Current state | Bright orange/accent border | Normal unless recently taken |
| Recently taken transition | Target remains current | Accent pulse fading over `0.20s` |
| Transitional graph playing | Target remains current; popup may note transition | Steady accent and thicker wire |
| Inactive | Dark existing border | Authored transition color |

Use `FStyleColors` where available and compatibility wrappers in `PaperZDEditorVersionCompatibility.h` where engine APIs differ. Avoid hard-coding a visual that only works against one editor theme.

### 6. Handle lifecycle and edge cases

Cover these explicitly:

- multiple live actors using the same AnimBP: only the toolbar-selected instance drives the graph;
- switching the selected instance while PIE/SIE is running;
- an actor or component being destroyed while selected;
- hot reload or AnimBP recompilation during PIE;
- nested/multiple state-machine runtime nodes with the same machine definition;
- two state-machine nodes with similar names;
- parallel transitions between the same states;
- multiple transitions/conduits traversed in one tick when `bAllowTransitionalStates` is enabled;
- constant transition rules and dynamic Blueprint rules;
- jump nodes;
- transitional graphs with no playable animation data;
- paused PIE/SIE, single-frame stepping, and time dilation;
- opening or closing the state-machine graph before, during, and after PIE/SIE.

Machine lookup should primarily use compiled indices/GUID metadata, not display names. If the same compiled state-machine definition can have more than one runtime node instance, extend the debug metadata with the animation-node property index and resolve that exact struct property rather than returning the first matching machine index.

## Verification plan

### Compiler/automation coverage

Add editor automation tests where the existing build setup permits:

- compile a machine with two states and one transition, then verify GUID-to-index mappings;
- compile parallel transitions and verify unique transition indices;
- compile a conduit chain and verify every visible edge is retained;
- delete/reorder nodes, recompile, and verify stale mappings are absent;
- duplicate a state machine and verify its mappings do not collide;
- verify older assets with absent debug metadata load and run without errors.

Add a focused runtime test for transition tracing: initial state, direct transition, multi-edge conduit path, chained transitions in one tick, transitional animation lifetime, and jump behavior.

### Manual PIE/SIE matrix

Create a small test AnimBP with Idle, Run, Jump, a conduit, parallel transitions, and one transitional animation. For both PIE and SIE:

1. Open the state-machine graph and select a live instance in the debug dropdown.
2. Confirm the initial state highlights and elapsed time increases.
3. Trigger each direct transition and confirm its edge pulse is visible.
4. Trigger the transitional animation and confirm its edge remains active until playback completes.
5. Traverse the conduit and confirm the complete accepted path highlights.
6. Switch between two actors in different states and confirm the graph switches immediately.
7. Pause and frame-step; confirm visualization is stable and deterministic.
8. Recompile during play and confirm there is no stale highlight or crash.
9. Destroy the selected actor and stop play; confirm the graph returns to its inactive appearance.

Also build the plugin for the oldest and newest supported Unreal Engine versions, because graph drawing parameters, style colors, and editor-only reflection guards vary between releases.

## Delivery sequence

Keep the work reviewable in four commits:

1. generated-class metadata and compiler mappings;
2. runtime transition snapshot and conduit-path tracing;
3. debug-object resolver and Slate visualization;
4. automation/manual test assets and compatibility fixes.

The first useful vertical slice is: select a PIE object, map one machine and state, and highlight the current state. Add transition persistence, transitional animation status, conduit paths, and richer popups after that slice is proven in both PIE and SIE.

## Acceptance criteria

The feature is complete when:

- a user can select any live PaperZD AnimBP instance from the normal Blueprint debug dropdown in PIE and SIE;
- the correct state is visibly active with elapsed time;
- direct, parallel, chained, and conduit transitions visibly identify the actual edge(s) taken;
- transitional animations keep their transition highlighted for their complete playback lifetime;
- changing/losing the debug object, recompiling, and ending play are safe and clear all live visualization;
- runtime behavior is unchanged when no editor debugger is attached;
- non-editor builds contain no meaningful tracing overhead;
- supported Unreal versions compile and the manual PIE/SIE matrix passes.
