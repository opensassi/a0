#!/usr/bin/env bash
# Generate docker tool descriptions for skills/system/docker/skill.json
set -euo pipefail

SKILL_JSON="skills/system/docker/skill.json"
mkdir -p "$(dirname "$SKILL_JSON")"

# Docker command descriptions
docker_commands() {
    docker --help 2>&1 | sed -n '/^  [a-z]/p' | awk -F'  +' '{print $1 "\t" $2}'
}

# Common management command groups that have sub-commands
management_groups="builder container context image network plugin system volume"

python3 << 'PYEOF'
import json, subprocess, sys, os

# Get docker commands from --help
result = subprocess.run(['docker', '--help'], capture_output=True, text=True)
cmds = {}
# Valid command pattern: lowercase alphabetic, possibly with hyphens
import re
cmd_pat = re.compile(r'^[a-z][a-z0-9-]*$')
current_section = ""
for line in result.stdout.split('\n'):
    if line.startswith('  ') and len(line.strip()) > 2:
        parts = line.strip().split(None, 1)
        if len(parts) == 2:
            name = parts[0].rstrip('*')
            if cmd_pat.match(name):
                cmds[name] = parts[1]
    elif line.strip().endswith('Commands:'):
        pass

# Known params for key docker commands
common_params = {
    'run': {'image': ('string', 'Image name'), 'command': ('string', 'Command to run'), 'detach': ('boolean', 'Run in background (-d)'), 'rm': ('boolean', 'Auto-remove on exit (--rm)'), 'name': ('string', 'Assign container name (--name)'), 'interactive': ('boolean', 'Keep STDIN open (-i)'), 'tty': ('boolean', 'Allocate pseudo-TTY (-t)'), 'publish': ('string', 'Publish port (-p host:container)'), 'volume': ('string', 'Bind mount volume (-v)'), 'env': ('string', 'Set environment variable (-e)'), 'network': ('string', 'Connect to network (--network)'), 'workdir': ('string', 'Working directory (-w)')},
    'exec': {'container': ('string', 'Container name/ID'), 'command': ('string', 'Command to execute'), 'interactive': ('boolean', 'Keep STDIN open (-i)'), 'tty': ('boolean', 'Allocate pseudo-TTY (-t)'), 'detach': ('boolean', 'Run in background (-d)'), 'env': ('string', 'Set environment variable (-e)')},
    'ps': {'all': ('boolean', 'Show all containers (-a)'), 'quiet': ('boolean', 'Only IDs (-q)'), 'filter': ('string', 'Filter output (--filter)'), 'format': ('string', 'Format output (--format)')},
    'build': {'tag': ('string', 'Tag image (-t)'), 'file': ('string', 'Dockerfile path (-f)'), 'no_cache': ('boolean', 'Do not use cache (--no-cache)'), 'pull': ('boolean', 'Always pull base images (--pull)')},
    'pull': {'image': ('string', 'Image name'), 'all_tags': ('boolean', 'Download all tags (-a)'), 'quiet': ('boolean', 'Suppress output (-q)')},
    'push': {'image': ('string', 'Image name'), 'all_tags': ('boolean', 'Push all tags (-a)')},
    'images': {'all': ('boolean', 'Show all images (-a)'), 'quiet': ('boolean', 'Only IDs (-q)'), 'filter': ('string', 'Filter (--filter)'), 'format': ('string', 'Format (--format)')},
    'stop': {'container': ('string', 'Container name/ID'), 'time': ('number', 'Seconds to wait before kill (-t)')},
    'start': {'container': ('string', 'Container name/ID')},
    'restart': {'container': ('string', 'Container name/ID'), 'time': ('number', 'Seconds to wait (-t)')},
    'rm': {'container': ('string', 'Container name/ID'), 'force': ('boolean', 'Force removal (-f)'), 'volumes': ('boolean', 'Remove volumes (-v)')},
    'logs': {'container': ('string', 'Container name/ID'), 'follow': ('boolean', 'Follow output (-f)'), 'tail': ('number', 'Tail lines (--tail=N)'), 'since': ('string', 'Show logs since (--since)')},
    'cp': {'source': ('string', 'Source path'), 'dest': ('string', 'Destination path')},
    'inspect': {'object': ('string', 'Container/image name/ID'), 'format': ('string', 'Format output (--format)')},
    'kill': {'container': ('string', 'Container name/ID'), 'signal': ('string', 'Signal to send (-s)')},
    'network': {'subcommand': ('string', 'Subcommand (create/rm/ls/connect/disconnect)'), 'name': ('string', 'Network name'), 'driver': ('string', 'Driver (-d)')},
    'volume': {'subcommand': ('string', 'Subcommand (create/rm/ls/prune)'), 'name': ('string', 'Volume name'), 'driver': ('string', 'Driver (-d)')},
    'login': {'username': ('string', 'Username (-u)'), 'password': ('string', 'Password (-p)'), 'server': ('string', 'Server URL')},
    'logout': {'server': ('string', 'Server URL')},
    'search': {'term': ('string', 'Search term'), 'limit': ('number', 'Max results (--limit)')},
    'tag': {'source': ('string', 'Source image'), 'target': ('string', 'Target tag')},
    'save': {'image': ('string', 'Image name'), 'output': ('string', 'Output file (-o)')},
    'load': {'input': ('string', 'Input file (-i)')},
    'export': {'container': ('string', 'Container name/ID'), 'output': ('string', 'Output file (-o)')},
    'import': {'file': ('string', 'Import file'), 'repository': ('string', 'Repository name')},
    'commit': {'container': ('string', 'Container name/ID'), 'repository': ('string', 'Repository name'), 'message': ('string', 'Commit message (-m)'), 'author': ('string', 'Author (--author)')},
    'history': {'image': ('string', 'Image name'), 'quiet': ('boolean', 'Only IDs (-q)'), 'no_trunc': ('boolean', 'Do not truncate (--no-trunc)')},
    'events': {'since': ('string', 'Show since (--since)'), 'until': ('string', 'Show until (--until)'), 'filter': ('string', 'Filter (--filter)')},
    'stats': {'container': ('string', 'Container name/ID'), 'no_stream': ('boolean', 'Disable streaming (--no-stream)'), 'all': ('boolean', 'Show all containers (-a)')},
}

def gen_tool_entry(cmd, desc):
    params = common_params.get(cmd, {})
    props = {}
    if cmd in ('container', 'image', 'network', 'volume', 'system', 'builder', 'context', 'plugin'):
        props['subcommand'] = {'type': 'string', 'description': f'{cmd} subcommand'}
    for pname, (ptype, pdesc) in sorted(params.items()):
        jtype = {'boolean': 'boolean', 'number': 'number', 'string': 'string'}.get(ptype, 'string')
        props[pname] = {'type': jtype, 'description': pdesc}
    props['args'] = {
        'type': 'array',
        'items': {'type': 'string'},
        'description': f'Additional arguments passed to docker {cmd}'
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
    'name': 'docker',
    'version': '1.0.0',
    'description': 'Docker container management tools — run, build, and manage containers',
    'tools': tools,
    'prompts': []
}

skill_json = os.environ.get('SKILL_JSON', 'skills/system/docker/skill.json')
with open(skill_json, 'w') as f:
    json.dump(result, f, indent=2)
    f.write('\n')
print(f'Generated {len(tools)} docker tools in {skill_json}')
PYEOF
