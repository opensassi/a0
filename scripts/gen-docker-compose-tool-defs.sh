#!/usr/bin/env bash
# Generate docker compose tool descriptions for skills/system/docker_compose/skill.json
set -euo pipefail

SKILL_JSON="skills/system/docker_compose/skill.json"
mkdir -p "$(dirname "$SKILL_JSON")"

python3 << 'PYEOF'
import json, subprocess, os

result = subprocess.run(['docker', 'compose', '--help'], capture_output=True, text=True)

cmds = {}
for line in result.stdout.split('\n'):
    if line.startswith('  ') and len(line.strip()) > 2:
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and parts[0].isalpha():
            cmds[parts[0]] = parts[1]

common_params = {
    'up': {'detach': ('boolean', 'Run in background (-d)'), 'build': ('boolean', 'Build before up (--build)'), 'force_recreate': ('boolean', 'Recreate containers (--force-recreate)'), 'remove_orphans': ('boolean', 'Remove orphans (--remove-orphans)'), 'wait': ('boolean', 'Wait for services ready (--wait)')},
    'down': {'volumes': ('boolean', 'Remove volumes (-v)'), 'images': ('string', 'Remove images (--images)'), 'remove_orphans': ('boolean', 'Remove orphans (--remove-orphans)')},
    'build': {'no_cache': ('boolean', 'Do not use cache (--no-cache)'), 'pull': ('boolean', 'Always pull (--pull)'), 'parallel': ('boolean', 'Build in parallel (--parallel)')},
    'pull': {'ignore_pull_failures': ('boolean', 'Ignore failures (--ignore-pull-failures)'), 'quiet': ('boolean', 'Quiet (--quiet)'), 'include_deps': ('boolean', 'Include deps (--include-deps)')},
    'push': {'ignore_push_failures': ('boolean', 'Ignore failures (--ignore-push-failures)')},
    'ps': {'all': ('boolean', 'Show stopped (-a)'), 'services': ('boolean', 'Show services (--services)'), 'filter': ('string', 'Filter (--filter)'), 'status': ('boolean', 'Show status (--status)')},
    'logs': {'follow': ('boolean', 'Follow (-f)'), 'tail': ('number', 'Tail lines (--tail=N)'), 'since': ('string', 'Show since (--since)'), 'timestamps': ('boolean', 'Show timestamps (-t)')},
    'exec': {'service': ('string', 'Service name'), 'command': ('string', 'Command to run'), 'interactive': ('boolean', 'Keep STDIN (-i)'), 'tty': ('boolean', 'Allocate TTY (-t)'), 'detach': ('boolean', 'Detached (-d)'), 'env': ('string', 'Set env (-e)')},
    'run': {'service': ('string', 'Service name'), 'command': ('string', 'Command to run'), 'rm': ('boolean', 'Remove after (--rm)'), 'detach': ('boolean', 'Detached (-d)'), 'entrypoint': ('string', 'Override entrypoint (--entrypoint)'), 'volume': ('string', 'Mount volume (-v)')},
    'restart': {'service': ('string', 'Service name'), 'no_deps': ('boolean', 'Skip deps (--no-deps)'), 'timeout': ('number', 'Timeout (-t)')},
    'start': {'service': ('string', 'Service name')},
    'stop': {'service': ('string', 'Service name'), 'timeout': ('number', 'Timeout (-t)')},
    'kill': {'service': ('string', 'Service name'), 'signal': ('string', 'Signal (-s)')},
    'pause': {'service': ('string', 'Service name')},
    'unpause': {'service': ('string', 'Service name')},
    'config': {'services': ('boolean', 'Print services (--services)'), 'volumes': ('boolean', 'Print volumes (--volumes)'), 'quiet': ('boolean', 'Valid only (-q)')},
    'top': {'service': ('string', 'Service name')},
    'images': {'quiet': ('boolean', 'Only IDs (-q)')},
    'port': {'service': ('string', 'Service name'), 'private_port': ('number', 'Private port')},
    'events': {'json': ('boolean', 'JSON output (--json)')},
    'cp': {'source': ('string', 'Source'), 'dest': ('string', 'Destination'), 'service': ('string', 'Service name'), 'index': ('number', 'Container index (--index)')},
    'create': {'service': ('string', 'Service name'), 'build': ('boolean', 'Build before (--build)'), 'no_recreate': ('boolean', 'No recreate (--no-recreate)'), 'force_recreate': ('boolean', 'Force recreate (--force-recreate)')},
    'scale': {'service': ('string', 'Service name'), 'replicas': ('number', 'Replica count')},
    'stats': {'all': ('boolean', 'Show all (--all)'), 'no_stream': ('boolean', 'No streaming (--no-stream)')},
    'version': {'short': ('boolean', 'Short version (--short)')},
    'wait': {'service': ('string', 'Service name'), 'timeout': ('number', 'Timeout (-t)')},
    'commit': {'service': ('string', 'Service name'), 'repository': ('string', 'Repository name'), 'message': ('string', 'Commit message (-m)'), 'author': ('string', 'Author (--author)')},
    'export': {'service': ('string', 'Service name'), 'output': ('string', 'Output file (-o)')},
    'attach': {'service': ('string', 'Service name'), 'index': ('number', 'Container index (--index)'), 'detach_keys': ('string', 'Detach keys (--detach-keys)')},
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
        'description': f'Additional arguments passed to docker compose {cmd}'
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

tools = []
for cmd, desc in sorted(cmds.items()):
    tools.append(gen_tool_entry(cmd, desc))

result = {
    'name': 'docker_compose',
    'version': '1.0.0',
    'description': 'Docker Compose tools — define and run multi-container applications',
    'tools': tools,
    'prompts': []
}

skill_json = os.environ.get('SKILL_JSON', 'skills/system/docker_compose/skill.json')
with open(skill_json, 'w') as f:
    json.dump(result, f, indent=2)
    f.write('\n')
print(f'Generated {len(tools)} docker compose tools in {skill_json}')
PYEOF
