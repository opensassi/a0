# DeepSeekProvider Spec

## Input/Output Contract
- `complete(systemPrompt, userPrompt)`: POST to `https://api.deepseek.com/v1/chat/completions`
  - Request body: `{"model":"deepseek-chat","messages":[{role:"system",content:systemPrompt},{role:"user",content:userPrompt}]}`
  - Response parsed: `choices[0].message.content`
- `setMockUrl(url)`: overrides base URL for testing

## Error Handling
- HTTP non-200 → throws `std::runtime_error` with status code
- Network error → throws `std::runtime_error` with curl error
- API returns error field → throws with error message
- Empty response content → returns empty string

## Edge Cases
- Very long prompts (>100K tokens) → sent as-is (API truncates)
- Mock URL with trailing slash → normalized
- Auth header missing → API returns 401, propagates as error
