# Git Sync

Synchronize with the remote repository.

## Steps

1. Fetch from origin:
   {{tool:system_git_fetch args='["origin"]'}}

2. Rebase onto origin/main:
   {{tool:system_git_rebase args='["origin/main"]'}}

3. Verify result:
   {{tool:system_git_status args='[]'}}

## Report
Summarize: sync result, current branch, tree state.
