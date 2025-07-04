#!/usr/bin/env python3
"""
Script to migrate create<T> usage to T::from_data with byte_reader
Replaces patterns like:
  auto result = create<Type>(data, version) -> auto result = Type::from_data(reader, version)
  auto result = create<Type>(version, data) -> auto result = Type::from_data(reader, version)
"""

import os
import re
import sys
from pathlib import Path

def find_cpp_test_files(base_path):
    """Find all .cpp test files in domain/test"""
    domain_test_path = Path(base_path) / "src" / "domain" / "test"
    if not domain_test_path.exists():
        print(f"Error: {domain_test_path} does not exist")
        return []
    
    cpp_files = []
    for root, dirs, files in os.walk(domain_test_path):
        for file in files:
            if file.endswith('.cpp'):
                cpp_files.append(os.path.join(root, file))
    return cpp_files

def migrate_create_patterns(content, file_path):
    """Migrate create<T> patterns to T::from_data patterns"""
    lines = content.split('\n')
    modified = False
    new_lines = []
    i = 0
    
    while i < len(lines):
        line = lines[i]
        
        # Pattern 1: auto result = create<Type>(version, data);
        pattern1 = re.search(r'(\s*)(auto\s+(?:const\s+)?)?(\w+)\s*=\s*create<([^>]+)>\(([^,]+),\s*([^)]+)\)\s*;', line)
        if pattern1:
            indent, auto_const, var_name, type_name, arg1, arg2 = pattern1.groups()
            
            # Determine which argument is data and which is version
            # Common patterns: create<Type>(version, data) or create<Type>(data, version)
            if 'data' in arg2.lower() or 'raw' in arg2.lower() or 'chunk' in arg2.lower():
                # Pattern: create<Type>(version, data)
                version_var, data_var = arg1, arg2
            else:
                # Pattern: create<Type>(data, version) 
                data_var, version_var = arg1, arg2
            
            auto_part = auto_const or "auto "
            new_lines.append(f'{indent}byte_reader reader({data_var});')
            new_lines.append(f'{indent}{auto_part}result_exp = {type_name}::from_data(reader, {version_var});')
            new_lines.append(f'{indent}REQUIRE(result_exp);')
            new_lines.append(f'{indent}{auto_part}{var_name} = std::move(*result_exp);')
            modified = True
            print(f"  Migrated create<{type_name}> with version in {file_path}:{i+1}")
            i += 1
            continue
        
        # Pattern 2: auto result = create<Type>(data);
        pattern2 = re.search(r'(\s*)(auto\s+(?:const\s+)?)?(\w+)\s*=\s*create<([^>]+)>\(([^)]+)\)\s*;', line)
        if pattern2:
            indent, auto_const, var_name, type_name, data_var = pattern2.groups()
            
            auto_part = auto_const or "auto "
            new_lines.append(f'{indent}byte_reader reader({data_var});')
            new_lines.append(f'{indent}{auto_part}result_exp = {type_name}::from_data(reader);')
            new_lines.append(f'{indent}REQUIRE(result_exp);')
            new_lines.append(f'{indent}{auto_part}{var_name} = std::move(*result_exp);')
            modified = True
            print(f"  Migrated create<{type_name}> without version in {file_path}:{i+1}")
            i += 1
            continue
        
        # Pattern 3: Variable assignment with create (e.g., instance = create<Type>(...))
        pattern3 = re.search(r'(\s*)(\w+)\s*=\s*create<([^>]+)>\(([^,]+),\s*([^)]+)\)\s*;', line)
        if pattern3:
            indent, var_name, type_name, arg1, arg2 = pattern3.groups()
            
            # Determine which argument is data and which is version
            if 'data' in arg2.lower() or 'raw' in arg2.lower() or 'chunk' in arg2.lower():
                version_var, data_var = arg1, arg2
            else:
                data_var, version_var = arg1, arg2
            
            new_lines.append(f'{indent}byte_reader reader({data_var});')
            new_lines.append(f'{indent}auto result_exp = {type_name}::from_data(reader, {version_var});')
            new_lines.append(f'{indent}REQUIRE(result_exp);')
            new_lines.append(f'{indent}{var_name} = std::move(*result_exp);')
            modified = True
            print(f"  Migrated create<{type_name}> assignment with version in {file_path}:{i+1}")
            i += 1
            continue
        
        # Pattern 4: Variable assignment with create (single argument)
        pattern4 = re.search(r'(\s*)(\w+)\s*=\s*create<([^>]+)>\(([^)]+)\)\s*;', line)
        if pattern4:
            indent, var_name, type_name, data_var = pattern4.groups()
            
            new_lines.append(f'{indent}byte_reader reader({data_var});')
            new_lines.append(f'{indent}auto result_exp = {type_name}::from_data(reader);')
            new_lines.append(f'{indent}REQUIRE(result_exp);')
            new_lines.append(f'{indent}{var_name} = std::move(*result_exp);')
            modified = True
            print(f"  Migrated create<{type_name}> assignment without version in {file_path}:{i+1}")
            i += 1
            continue
        
        # No pattern matched, keep the line as is
        new_lines.append(line)
        i += 1
    
    return '\n'.join(new_lines), modified

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 migrate_create_patterns.py <kth-mono-path>")
        sys.exit(1)
    
    base_path = sys.argv[1]
    if not os.path.exists(base_path):
        print(f"Error: {base_path} does not exist")
        sys.exit(1)
    
    cpp_files = find_cpp_test_files(base_path)
    if not cpp_files:
        print("No .cpp test files found")
        sys.exit(1)
    
    print(f"Found {len(cpp_files)} .cpp test files")
    
    total_modified = 0
    for file_path in cpp_files:
        print(f"\nProcessing: {file_path}")
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Check if file contains create< patterns
            if 'create<' not in content:
                continue
            
            new_content, modified = migrate_create_patterns(content, file_path)
            
            if modified:
                # # Create backup
                # backup_path = file_path + '.backup_create'
                # with open(backup_path, 'w', encoding='utf-8') as f:
                #     f.write(content)
                
                # Write new content
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                
                total_modified += 1
                print(f"  ✓ Modified {file_path} (backup: {backup_path})")
            else:
                print(f"  - No changes needed for {file_path}")
                
        except Exception as e:
            print(f"  ✗ Error processing {file_path}: {e}")
    
    print(f"\n=== Summary ===")
    print(f"Total files processed: {len(cpp_files)}")
    print(f"Files modified: {total_modified}")
    print(f"Backup files created with .backup_create extension")

if __name__ == "__main__":
    main()
