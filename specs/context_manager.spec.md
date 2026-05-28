# ContextManager Spec

## Input/Output Contract
- `push(frame)`: appends to internal stack
- `pop()`: removes and returns top frame; throws on empty
- `peek()`: returns top frame without removing; throws on empty
- `size()`: returns current stack depth
- `clear()`: removes all frames
- `snapshot()`: returns copy of all frames

## Error Handling
- Pop on empty stack → throws `std::out_of_range`
- Peek on empty stack → throws `std::out_of_range`

## Edge Cases
- Push after clear → stack has one element
- Maximum depth (1000) → push beyond limit silently drops oldest
- Empty snapshot → returns empty vector
