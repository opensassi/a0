#!/usr/bin/env bash
# Generate git tool descriptions for skills/system/git/skill.json
# Uses git --list-cmds=main for accurate command enumeration.
set -euo pipefail

SKILL_JSON="skills/system/git/skill.json"

# Get all git commands
ALL_CMDS=$(git --list-cmds=main 2>/dev/null)

# Build description map from git help -a
declare -A DESC_MAP
while IFS=$'\t' read -r cmd desc; do
    [[ -z "$cmd" ]] && continue
    DESC_MAP["$cmd"]="$desc"
done < <(git help --all 2>/dev/null | sed -n '/^  [a-z]/p' | sed 's/^  //' | awk -F'  +' '{print $1 "\t" $2}')

# Generate JSON for all tools using Python
python3 << 'PYEOF'
import json, subprocess, sys, os

# Get the description map from parent shell
desc_map_raw = {}
result = subprocess.run(['git', 'help', '--all'], capture_output=True, text=True)
for line in result.stdout.split('\n'):
    if line.startswith('  ') and len(line.strip()) > 2:
        parts = line.strip().split(None, 1)
        if len(parts) == 2:
            desc_map_raw[parts[0]] = parts[1]

# Get command list
result = subprocess.run(['git', '--list-cmds=main'], capture_output=True, text=True)
cmds = [c.strip() for c in result.stdout.strip().split('\n') if c.strip()]

# Parameter definitions for well-known commands
common_params = {
    'add': {'all': ('boolean', 'Stage all changes (-A)'), 'verbose': ('boolean', 'Show verbose output (-v)'), 'dry_run': ('boolean', 'Show what would be added (--dry-run)')},
    'am': {'3way': ('boolean', 'Allow 3-way merge (--3way)'), 'signoff': ('boolean', 'Add Signed-off-by line (-s)')},
    'archive': {'format': ('string', 'Archive format (--format=tar|zip)'), 'output': ('string', 'Write to file (--output=FILE)')},
    'bisect': {'start': ('boolean', 'Start bisect session'), 'good': ('string', 'Mark commit as good'), 'bad': ('string', 'Mark commit as bad'), 'reset': ('boolean', 'Reset bisect state')},
    'branch': {'delete': ('string', 'Delete branch (-d)'), 'force': ('boolean', 'Force delete/update'), 'move': ('boolean', 'Move/rename branch (-m)'), 'list': ('boolean', 'List branches'), 'all': ('boolean', 'List all branches (-a)')},
    'checkout': {'branch': ('string', 'Branch to checkout'), 'b': ('string', 'Create and checkout new branch (-b)'), 'force': ('boolean', 'Force checkout (-f)'), 'merge': ('boolean', 'Merge into new branch (--merge)')},
    'cherry-pick': {'commit': ('string', 'Commit to cherry-pick'), 'no_commit': ('boolean', "Don't auto-commit (-n)"), 'signoff': ('boolean', 'Add Signed-off-by (-s)'), 'edit': ('boolean', 'Edit commit message (-e)')},
    'clean': {'force': ('boolean', 'Force removal (-f)'), 'dry_run': ('boolean', 'Show what would be removed (-n)'), 'interactive': ('boolean', 'Interactive mode (-i)')},
    'clone': {'url': ('string', 'Repository URL'), 'directory': ('string', 'Target directory'), 'depth': ('number', 'Shallow clone depth (--depth=N)'), 'branch': ('string', 'Clone specific branch (-b)'), 'recurse_submodules': ('boolean', 'Clone submodules')},
    'commit': {'message': ('string', 'Commit message (-m)'), 'all': ('boolean', 'Auto-stage modified/deleted (-a)'), 'amend': ('boolean', 'Amend last commit (--amend)'), 'no_verify': ('boolean', 'Skip hooks (--no-verify)'), 'allow_empty': ('boolean', 'Allow empty commit'), 'signoff': ('boolean', 'Add Signed-off-by (-s)')},
    'config': {'name': ('string', 'Config key'), 'value': ('string', 'Value to set'), 'global': ('boolean', 'Use global config (--global)'), 'local': ('boolean', 'Use local config (--local)'), 'list': ('boolean', 'List all config (-l)')},
    'describe': {'tags': ('boolean', 'Use any ref (--tags)'), 'all': ('boolean', 'Use any ref (--all)'), 'dirty': ('string', 'Suffix for dirty tree')},
    'diff': {'cached': ('boolean', 'Show staged changes (--cached)'), 'name_only': ('boolean', 'Show only names (--name-only)'), 'stat': ('boolean', 'Show diffstat (--stat)')},
    'fetch': {'remote': ('string', 'Remote name'), 'branch': ('string', 'Branch to fetch'), 'all': ('boolean', 'Fetch all remotes (--all)'), 'prune': ('boolean', 'Prune deleted refs (-p)'), 'depth': ('number', 'Depth of fetch')},
    'gc': {'aggressive': ('boolean', 'Aggressive optimization'), 'auto': ('boolean', 'Run only if needed (--auto)')},
    'init': {'directory': ('string', 'Directory to initialize'), 'bare': ('boolean', 'Create bare repository (--bare)'), 'initial_branch': ('string', 'Initial branch name')},
    'log': {'max_count': ('number', 'Limit commits (-n N)'), 'oneline': ('boolean', 'One line per commit (--oneline)'), 'graph': ('boolean', 'Show ASCII graph (--graph)'), 'all': ('boolean', 'Show all branches (--all)'), 'author': ('string', 'Filter by author'), 'since': ('string', 'Show commits since (--since=DATE)'), 'until': ('string', 'Show commits until (--until=DATE)')},
    'merge': {'branch': ('string', 'Branch to merge'), 'no_ff': ('boolean', 'Create merge commit (--no-ff)'), 'squash': ('boolean', 'Squash commits (--squash)'), 'abort': ('boolean', 'Abort merge'), 'continue': ('boolean', 'Continue merge')},
    'pull': {'remote': ('string', 'Remote name'), 'branch': ('string', 'Branch to pull'), 'rebase': ('boolean', 'Rebase instead of merge (--rebase)'), 'ff_only': ('boolean', 'Only allow fast-forward (--ff-only)')},
    'push': {'remote': ('string', 'Remote name'), 'branch': ('string', 'Branch to push'), 'force': ('boolean', 'Force push (-f)'), 'force_with_lease': ('boolean', 'Force with lease'), 'set_upstream': ('boolean', 'Set upstream (-u)'), 'tags': ('boolean', 'Push tags'), 'all': ('boolean', 'Push all branches')},
    'rebase': {'onto': ('string', 'Upstream branch (--onto)'), 'branch': ('string', 'Branch to rebase'), 'interactive': ('boolean', 'Interactive mode (-i)'), 'abort': ('boolean', 'Abort rebase'), 'continue': ('boolean', 'Continue rebase'), 'skip': ('boolean', 'Skip current commit'), 'autosquash': ('boolean', 'Auto-squash fixup commits')},
    'remote': {'add': ('string', 'Add remote name'), 'url': ('string', 'Remote URL'), 'remove': ('string', 'Remove remote'), 'rename': ('string', 'Rename remote old name'), 'verbose': ('boolean', 'Show verbose (-v)')},
    'reset': {'target': ('string', 'Commit/branch to reset to'), 'soft': ('boolean', 'Soft reset (--soft)'), 'mixed': ('boolean', 'Mixed reset (--mixed)'), 'hard': ('boolean', 'Hard reset (--hard)')},
    'restore': {'source': ('string', 'Source tree (-s)'), 'staged': ('boolean', 'Restore the index (--staged)'), 'worktree': ('boolean', 'Restore working tree (--worktree)'), 'ours': ('boolean', 'Use our version on conflicts'), 'theirs': ('boolean', 'Use their version on conflicts')},
    'revert': {'commit': ('string', 'Commit to revert'), 'no_commit': ('boolean', "Don't auto-commit (-n)"), 'edit': ('boolean', 'Edit commit message (-e)')},
    'shortlog': {'max_count': ('number', 'Limit commits (-n N)'), 'numbered': ('boolean', 'Number entries (-n)'), 'summary': ('boolean', 'Suppress commit descriptions (-s)')},
    'show': {'object': ('string', 'Object to show'), 'name_only': ('boolean', 'Show only names (--name-only)'), 'stat': ('boolean', 'Show diffstat (--stat)')},
    'stash': {'push': ('boolean', 'Stash changes'), 'pop': ('boolean', 'Apply and remove top stash'), 'apply': ('string', 'Apply stash'), 'drop': ('string', 'Drop stash'), 'list': ('boolean', 'List stashes'), 'show': ('string', 'Show stash diff'), 'include_untracked': ('boolean', 'Include untracked (-u)')},
    'status': {'short': ('boolean', 'Short format (-s)'), 'branch': ('boolean', 'Show branch info (-b)'), 'porcelain': ('number', 'Porcelain format version')},
    'submodule': {'add': ('string', 'Add submodule URL'), 'init': ('boolean', 'Initialize submodules'), 'update': ('boolean', 'Update submodules'), 'recursive': ('boolean', 'Recurse into submodules'), 'remote': ('boolean', 'Track remote branches')},
    'switch': {'branch': ('string', 'Branch to switch to'), 'create': ('string', 'Create and switch to new branch (-c)'), 'force': ('boolean', 'Force switch (-f)'), 'detach': ('boolean', 'Detach HEAD (--detach)')},
    'tag': {'name': ('string', 'Tag name'), 'target': ('string', 'Object to tag'), 'delete': ('string', 'Delete tag (-d)'), 'list': ('string', 'List tags (-l PATTERN)'), 'annotate': ('boolean', 'Create annotated tag (-a)'), 'message': ('string', 'Tag message (-m)'), 'force': ('boolean', 'Force tag (-f)'), 'sign': ('boolean', 'GPG-sign tag (-s)')},
    'worktree': {'add': ('string', 'Path for new working tree'), 'list': ('boolean', 'List linked working trees'), 'prune': ('boolean', 'Prune stale working trees'), 'remove': ('string', 'Remove a working tree')},
    'blame': {'file': ('string', 'File to blame'), 'revision': ('string', 'Start from revision'), 'porcelain': ('boolean', 'Porcelain format'), 'show_email': ('boolean', 'Show email (-e)')},
    'help': {'command': ('string', 'Command to get help for'), 'all': ('boolean', 'List all commands (-a)'), 'guides': ('boolean', 'List guide pages (-g)')},
}

def gen_tool_entry(cmd, desc):
    params = common_params.get(cmd, {})
    props = {}
    for pname, (ptype, pdesc) in sorted(params.items()):
        jtype = {'boolean': 'boolean', 'number': 'number', 'string': 'string'}.get(ptype, 'string')
        props[pname] = {'type': jtype, 'description': pdesc}
    props['args'] = {
        'type': 'array',
        'items': {'type': 'string'},
        'description': f'Additional arguments passed to git {cmd}'
    }
    return {
        'name': cmd,
        'description': desc or '',
        'systemTool': True,
        'parameters': {
            'type': 'object',
            'properties': props,
            'required': []
        }
    }

tools = [gen_tool_entry(cmd, desc_map_raw.get(cmd, '')) for cmd in cmds]

# Read existing skill.json
with open(os.environ.get('SKILL_JSON', 'skills/system/git/skill.json')) as f:
    existing = json.load(f)

result = {
    'name': existing['name'],
    'version': existing['version'],
    'description': existing['description'],
    'tools': tools,
    'prompts': existing.get('prompts', [])
}

print(f'Validated: {len(tools)} tools, {len(result["prompts"])} prompts')

with open(os.environ.get('SKILL_JSON', 'skills/system/git/skill.json'), 'w') as f:
    json.dump(result, f, indent=2)
    f.write('\n')
PYEOF

echo "Updated $SKILL_JSON"
