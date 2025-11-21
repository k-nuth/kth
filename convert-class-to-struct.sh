#!/bin/bash
# Script to convert class to struct for syntactic simplification
#
# Two patterns:
# 1. class Foo {
#    public:
#    => struct Foo {
#    (remove the public: line)
#
# 2. class Foo : public Bar
#    => struct Foo : Bar
#    (remove public from inheritance)

set -e

DRY_RUN=true
if [ "$1" = "--apply" ]; then
    DRY_RUN=false
    echo "🔧 APPLY MODE - Changes will be written to files"
else
    echo "🔍 DRY RUN MODE - No changes will be made"
    echo "Run with --apply to actually modify files"
fi

echo ""
echo "==================================================="
echo "Pattern 1: class X_API name {"
echo "           public:"
echo "        => struct X_API name {"
echo "==================================================="

count1=0
# Find files with pattern 1
if [ "$DRY_RUN" = true ]; then
    python3 << 'PYEOF'
import re
import os

dry_run = True

pattern = re.compile(r'^(\s*)(class\s+(K[A-Z]*_API)\s+(\w+)\s*\{)\s*$')
public_pattern = re.compile(r'^\s*public:\s*$')

count = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith(('.hpp', '.h')):
            filepath = os.path.join(root, file)
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()

                modified = False
                new_lines = []
                i = 0
                while i < len(lines):
                    match = pattern.match(lines[i])
                    if match and i + 1 < len(lines):
                        # Check if next line is public:
                        if public_pattern.match(lines[i+1]):
                            indent = match.group(1)
                            api = match.group(3)
                            name = match.group(4)
                            new_line = f"{indent}struct {api} {name} {{\n"
                            new_lines.append(new_line)
                            count += 1
                            modified = True
                            print(f"{filepath}:{i+1}")
                            print(f"  - {lines[i].rstrip()}")
                            print(f"  - {lines[i+1].rstrip()}")
                            print(f"  + {new_line.rstrip()}")
                            print()
                            i += 2  # Skip the public: line
                            continue
                    new_lines.append(lines[i])
                    i += 1

                if modified and not dry_run:
                    with open(filepath, 'w', encoding='utf-8') as f:
                        f.writelines(new_lines)
            except Exception as e:
                pass

print(f"\nPattern 1: Found {count} occurrences")
PYEOF
else
    python3 << 'PYEOF'
import re
import os

dry_run = False

pattern = re.compile(r'^(\s*)(class\s+(K[A-Z]*_API)\s+(\w+)\s*\{)\s*$')
public_pattern = re.compile(r'^\s*public:\s*$')

count = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith(('.hpp', '.h')):
            filepath = os.path.join(root, file)
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()

                modified = False
                new_lines = []
                i = 0
                while i < len(lines):
                    match = pattern.match(lines[i])
                    if match and i + 1 < len(lines):
                        # Check if next line is public:
                        if public_pattern.match(lines[i+1]):
                            indent = match.group(1)
                            api = match.group(3)
                            name = match.group(4)
                            new_line = f"{indent}struct {api} {name} {{\n"
                            new_lines.append(new_line)
                            count += 1
                            modified = True
                            print(f"{filepath}:{i+1}")
                            print(f"  - {lines[i].rstrip()}")
                            print(f"  - {lines[i+1].rstrip()}")
                            print(f"  + {new_line.rstrip()}")
                            print()
                            i += 2  # Skip the public: line
                            continue
                    new_lines.append(lines[i])
                    i += 1

                if modified:
                    with open(filepath, 'w', encoding='utf-8') as f:
                        f.writelines(new_lines)
            except Exception as e:
                pass

print(f"\nPattern 1: Found {count} occurrences")
PYEOF
fi

echo ""
echo "==================================================="
echo "Pattern 2: class X_API name : public Base"
echo "        => struct X_API name : Base"
echo "==================================================="

# Find files with pattern 2
if [ "$DRY_RUN" = true ]; then
    python3 << 'PYEOF'
import re
import os

dry_run = True

# Pattern: class X_API name : public Base
pattern = re.compile(r'^(\s*)(class\s+(K[A-Z]*_API)\s+(\w+)\s*:\s*public\s+(.+))$')

def remove_public_from_bases(bases):
    return re.sub(r'\bpublic\s+', '', bases)

count = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith(('.hpp', '.h', '.cpp')):
            filepath = os.path.join(root, file)
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()

                modified = False
                new_lines = []
                for i, line in enumerate(lines):
                    match = pattern.match(line)
                    if match:
                        indent = match.group(1)
                        api = match.group(3)
                        name = match.group(4)
                        base = remove_public_from_bases(match.group(5))
                        new_line = f"{indent}struct {api} {name} : {base}\n"
                        new_lines.append(new_line)
                        count += 1
                        modified = True
                        print(f"{filepath}:{i+1}")
                        print(f"  - {line.rstrip()}")
                        print(f"  + {new_line.rstrip()}")
                        print()
                    else:
                        new_lines.append(line)

                if modified and not dry_run:
                    with open(filepath, 'w', encoding='utf-8') as f:
                        f.writelines(new_lines)
            except Exception as e:
                pass

print(f"\nPattern 2: Found {count} occurrences")
PYEOF
else
    python3 << 'PYEOF'
import re
import os

dry_run = False

# Pattern: class X_API name : public Base
pattern = re.compile(r'^(\s*)(class\s+(K[A-Z]*_API)\s+(\w+)\s*:\s*public\s+(.+))$')

def remove_public_from_bases(bases):
    return re.sub(r'\bpublic\s+', '', bases)

count = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith(('.hpp', '.h', '.cpp')):
            filepath = os.path.join(root, file)
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()

                modified = False
                new_lines = []
                for i, line in enumerate(lines):
                    match = pattern.match(line)
                    if match:
                        indent = match.group(1)
                        api = match.group(3)
                        name = match.group(4)
                        base = remove_public_from_bases(match.group(5))
                        new_line = f"{indent}struct {api} {name} : {base}\n"
                        new_lines.append(new_line)
                        count += 1
                        modified = True
                        print(f"{filepath}:{i+1}")
                        print(f"  - {line.rstrip()}")
                        print(f"  + {new_line.rstrip()}")
                        print()
                    else:
                        new_lines.append(line)

                if modified:
                    with open(filepath, 'w', encoding='utf-8') as f:
                        f.writelines(new_lines)
            except Exception as e:
                pass

print(f"\nPattern 2: Found {count} occurrences")
PYEOF
fi

echo ""
echo "==================================================="
if [ "$DRY_RUN" = true ]; then
    echo "✅ Dry run complete. Run with --apply to make changes."
else
    echo "✅ Changes applied!"
fi
echo "==================================================="
