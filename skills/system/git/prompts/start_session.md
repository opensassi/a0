# Git Start Session

You are starting a new git session. Follow these steps in order:

## Current State
{{tool:system_git_status args='[]'}}

## Steps
1. Switch to main branch:
   {{tool:system_git_checkout args='["main"]'}}

2. Pull latest with rebase:
   {{tool:system_git_pull args='["--rebase", "origin", "main"]'}}

3. Verify clean state:
   {{tool:system_git_status args='[]'}}

## Report
Summarize: what branch are you on, is the tree clean, any issues encountered?
