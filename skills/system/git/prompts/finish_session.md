# Git Finish Session

You are finishing a git session. Follow these steps in order.

## Current State
{{tool:system_git_status args='[]'}}

## Steps

1. Stage all changes:
   {{tool:system_git_add args='["."]'}}

2. Check status after staging:
   {{tool:system_git_status args='[]'}}

3. Commit changes (if any):
   {{tool:system_git_commit args='["-m", "SESSION_MESSAGE"]'}}

4. Switch to main and rebase:
   {{tool:system_git_checkout args='["main"]'}}
   {{tool:system_git_pull args='["--rebase", "origin", "main"]'}}

5. Switch back to feature branch and rebase onto main:
   {{tool:system_git_rebase args='["main"]'}}

6. Push changes:
   {{tool:system_git_push args='["origin", "CURRENT_BRANCH"]'}}

## Report
Summarize: what was committed, any conflicts resolved, push result.
