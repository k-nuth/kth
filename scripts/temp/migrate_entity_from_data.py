#!/usr/bin/env python3
"""
Script to migrate entity_from_data usage to T::from_data with byte_reader
Replaces patterns like:
  entity_from_data(instance, data, version) -> T::from_data(reader, version)
  entity_from_data(instance, data) -> T::from_data(reader)
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

def extract_type_from_test_case(content, line_num):
    """Try to extract the type T from the test case context"""
    lines = content.split('\n')
    
    # Look backwards from the entity_from_data line to find type hints
    for i in range(max(0, line_num - 10), line_num):
        line = lines[i].strip()
        
        # Pattern: Type instance;
        type_match = re.search(r'(\w+(?:::\w+)*)\s+\w+;', line)
        if type_match:
            return type_match.group(1)
        
        # Pattern: const Type expected{...}
        const_match = re.search(r'const\s+(\w+(?:::\w+)*)\s+\w+', line)
        if const_match:
            return const_match.group(1)
        
        # Pattern: Type expected{...}
        type_init_match = re.search(r'^(\w+(?:::\w+)*)\s+\w+\{', line)
        if type_init_match:
            return type_init_match.group(1)
    
    return None

def is_variable_declared(lines, var_name, type_name, current_line):
    """Check if a variable is already declared in previous lines within the same test case"""
    # Look backwards from current line to find variable declaration
    test_case_start = current_line
    
    # Find the start of the current TEST_CASE
    for i in range(current_line - 1, -1, -1):
        if 'TEST_CASE(' in lines[i]:
            test_case_start = i
            break
    
    # Look for variable declaration between test case start and current line
    for i in range(test_case_start, current_line):
        line = lines[i].strip()
        # Pattern: Type varname; or Type varname{...} or Type varname(...)
        if re.search(rf'{re.escape(type_name)}\s+{re.escape(var_name)}\s*[;{{(]', line):
            return True
        # Pattern: auto varname or const Type varname
        if re.search(rf'(auto|const\s+{re.escape(type_name)})\s+{re.escape(var_name)}\s*[;={{]', line):
            return True
    
    return False

def migrate_entity_from_data(content, file_path):
    """Migrate entity_from_data patterns to T::from_data patterns"""
    lines = content.split('\n')
    modified = False
    new_lines = []
    i = 0
    
    while i < len(lines):
        line = lines[i]
        
        # Pattern 1: REQUIRE( ! entity_from_data(instance, data, version));
        pattern1 = re.search(r'(\s*)REQUIRE\(\s*!\s*entity_from_data\((\w+),\s*([^,]+),\s*([^)]+)\)\s*\);', line)
        if pattern1:
            indent, instance_name, data_var, version_var = pattern1.groups()
            
            # Try to extract type from context
            type_name = extract_type_from_test_case(content, i)
            if type_name:
                new_lines.append(f'{indent}byte_reader reader({data_var});')
                new_lines.append(f'{indent}auto result = {type_name}::from_data(reader, {version_var});')
                new_lines.append(f'{indent}REQUIRE( ! result);')
                modified = True
                print(f"  Migrated negative entity_from_data in {file_path}:{i+1}")
            else:
                new_lines.append(line)
                print(f"  WARNING: Could not determine type for line {i+1} in {file_path}")
            i += 1
            continue
        
        # Pattern 2: REQUIRE(entity_from_data(instance, data, version));
        pattern2 = re.search(r'(\s*)REQUIRE\(\s*entity_from_data\((\w+),\s*([^,]+),\s*([^)]+)\)\s*\);', line)
        if pattern2:
            indent, instance_name, data_var, version_var = pattern2.groups()
            
            # Try to extract type from context
            type_name = extract_type_from_test_case(content, i)
            if type_name:
                # Check if instance is declared on previous lines
                instance_declared = is_variable_declared(lines, instance_name, type_name, i)
                
                if instance_declared:
                    # Instance already declared, assign to it
                    new_lines.append(f'{indent}byte_reader reader({data_var});')
                    new_lines.append(f'{indent}auto result = {type_name}::from_data(reader, {version_var});')
                    new_lines.append(f'{indent}REQUIRE(result);')
                    new_lines.append(f'{indent}{instance_name} = std::move(*result);')
                else:
                    # Instance not declared, declare and initialize in one line
                    new_lines.append(f'{indent}byte_reader reader({data_var});')
                    new_lines.append(f'{indent}auto result = {type_name}::from_data(reader, {version_var});')
                    new_lines.append(f'{indent}REQUIRE(result);')
                    new_lines.append(f'{indent}{type_name} {instance_name} = std::move(*result);')
                
                modified = True
                print(f"  Migrated positive entity_from_data in {file_path}:{i+1}")
            else:
                new_lines.append(line)
                print(f"  WARNING: Could not determine type for line {i+1} in {file_path}")
            i += 1
            continue
        
        # Pattern 3: REQUIRE( ! entity_from_data(instance, data));
        pattern3 = re.search(r'(\s*)REQUIRE\(\s*!\s*entity_from_data\((\w+),\s*([^)]+)\)\s*\);', line)
        if pattern3:
            indent, instance_name, data_var = pattern3.groups()
            
            # Try to extract type from context
            type_name = extract_type_from_test_case(content, i)
            if type_name:
                new_lines.append(f'{indent}byte_reader reader({data_var});')
                new_lines.append(f'{indent}auto result = {type_name}::from_data(reader);')
                new_lines.append(f'{indent}REQUIRE( ! result);')
                modified = True
                print(f"  Migrated negative entity_from_data (no version) in {file_path}:{i+1}")
            else:
                new_lines.append(line)
                print(f"  WARNING: Could not determine type for line {i+1} in {file_path}")
            i += 1
            continue
        
        # Pattern 4: REQUIRE(entity_from_data(instance, data));
        pattern4 = re.search(r'(\s*)REQUIRE\(\s*entity_from_data\((\w+),\s*([^)]+)\)\s*\);', line)
        if pattern4:
            indent, instance_name, data_var = pattern4.groups()
            
            # Try to extract type from context
            type_name = extract_type_from_test_case(content, i)
            if type_name:
                # Check if instance is declared on previous lines
                instance_declared = is_variable_declared(lines, instance_name, type_name, i)
                
                if instance_declared:
                    # Instance already declared, assign to it
                    new_lines.append(f'{indent}byte_reader reader({data_var});')
                    new_lines.append(f'{indent}auto result = {type_name}::from_data(reader);')
                    new_lines.append(f'{indent}REQUIRE(result);')
                    new_lines.append(f'{indent}{instance_name} = std::move(*result);')
                else:
                    # Instance not declared, declare and initialize in one line
                    new_lines.append(f'{indent}byte_reader reader({data_var});')
                    new_lines.append(f'{indent}auto result = {type_name}::from_data(reader);')
                    new_lines.append(f'{indent}REQUIRE(result);')
                    new_lines.append(f'{indent}{type_name} {instance_name} = std::move(*result);')
                
                modified = True
                print(f"  Migrated positive entity_from_data (no version) in {file_path}:{i+1}")
            else:
                new_lines.append(line)
                print(f"  WARNING: Could not determine type for line {i+1} in {file_path}")
            i += 1
            continue
        
        # Pattern 5: bool result = entity_from_data(...)
        pattern5 = re.search(r'(\s*)bool\s+\w+\s*=\s*entity_from_data\((\w+),\s*([^,]+),\s*([^)]+)\)\s*;', line)
        if pattern5:
            indent, instance_name, data_var, version_var = pattern5.groups()
            
            # Try to extract type from context
            type_name = extract_type_from_test_case(content, i)
            if type_name:
                new_lines.append(f'{indent}byte_reader reader({data_var});')
                new_lines.append(f'{indent}auto exp_result = {type_name}::from_data(reader, {version_var});')
                new_lines.append(f'{indent}bool result = static_cast<bool>(exp_result);')
                new_lines.append(f'{indent}if (result) {instance_name} = std::move(*exp_result);')
                modified = True
                print(f"  Migrated bool entity_from_data in {file_path}:{i+1}")
            else:
                new_lines.append(line)
                print(f"  WARNING: Could not determine type for line {i+1} in {file_path}")
            i += 1
            continue
        
        # No pattern matched, keep the line as is
        new_lines.append(line)
        i += 1
    
    return '\n'.join(new_lines), modified

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 migrate_entity_from_data.py <kth-mono-path>")
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
            
            # Check if file contains entity_from_data
            if 'entity_from_data' not in content:
                continue
            
            new_content, modified = migrate_entity_from_data(content, file_path)
            
            if modified:
                # Create backup
                backup_path = file_path + '.backup'
                with open(backup_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                
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
    print(f"Backup files created with .backup extension")

if __name__ == "__main__":
    main()
