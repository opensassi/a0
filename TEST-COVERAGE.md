# Test Coverage Report

Generated 2026-06-05T23:14:23Z — combined across 32 unit + 10 agent E2E + 14 TUI E2E tests

## Overall
```
Summary coverage rate:
  lines......: 46.1% (4281 of 9290 lines)
  functions..: 47.6% (675 of 1419 functions)
  branches...: no data found
```

## Per-File Coverage (source only, ascending)
```
src/unix_socket.h                              |    -     0|    -   0|    -    0
src/tui/input_panel.h                          | 100%     1| 0.0%   1|    -    0
src/tui/agent_tui.h                            | 100%     3| 0.0%   3|    -    0
src/tool_state.cpp                             |21.7%    23| 0.0%   5|    -    0
src/tool_runner.cpp                            | 8.6%    58| 0.0%   5|    -    0
src/system_handlers.cpp                        | 5.8%   344| 0.0%  18|    -    0
src/skills/version_manager.cpp                 | 8.5%   130| 0.0%  11|    -    0
src/skills/validation_engine.cpp               | 6.5%   107| 0.0%   7|    -    0
src/skills/skills.h                            |    -     0|    -   0|    -    0
src/skills/skill_manager.cpp                   |12.1%   315| 0.0%  31|    -    0
src/skills/skill_loader.cpp                    | 6.2%   307| 0.0%  19|    -    0
src/session_context.h                          | 100%     2| 0.0%   2|    -    0
src/session_context.cpp                        |10.5%    76| 0.0%   8|    -    0
src/response_decoder.h                         | 100%     2| 0.0%   2|    -    0
src/response_decoder.cpp                       | 5.7%   122| 0.0%   6|    -    0
src/persistence/persistence_store.h            | 200%     1| 0.0%   1|    -    0
src/mpsc.h                                     |44.6%    65| 0.0%  29|    -    0
src/main.cpp                                   |11.6%   320| 0.0%  18|    -    0
src/llm_provider.h                             | 200%     1| 0.0%   1|    -    0
src/hex_session_id.h                           | 9.1%    11| 0.0%   1|    -    0
src/driven_provider.h                          | 100%     2| 0.0%   2|    -    0
src/driven_provider.cpp                        |10.4%   115| 0.0%  10|    -    0
src/driven_core.h                              |40.0%     5| 0.0%   2|    -    0
src/driven_core.cpp                            | 7.8%   180| 0.0%  11|    -    0
src/docker_security_filter.cpp                 | 167%     3| 0.0%   1|    -    0
src/docker/container_manager.h                 |    -     0|    -   0|    -    0
src/dependency_resolver.cpp                    |18.5%    27| 0.0%   5|    -    0
src/dependency_graph.cpp                       |10.0%    70| 0.0%   5|    -    0
src/deepseek_provider.cpp                      | 6.7%    45| 0.0%   3|    -    0
src/context_manager.cpp                        |27.3%    22| 0.0%   6|    -    0
src/base_prompt.cpp                            |14.0%    50| 0.0%   7|    -    0
src/app_core_thread.h                          | 100%     2| 0.0%   2|    -    0
src/app_core_thread.cpp                        | 6.0%   100| 0.0%   5|    -    0
src/agent_interfaces.h                         | 140%    10| 0.0%   8|    -    0
src/a0_dir.cpp                                 |15.9%    44| 0.0%   6|    -    0
```

## Notes
- Branch coverage not available (multi-process .gcda merging can produce artifacts)
- Thirdparty code (valijson, etc.) included in line counts
