#!/usr/bin/env python3
"""
Script to remove TEST_CASE blocks that use data_source and/or istream_reader
These tests are obsolete and should be completely removed.
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

def find_test_case_boundaries(lines):
    """Find start and end of each TEST_CASE block"""
    test_cases = []
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith('TEST_CASE('):
            start = i
            brace_count = 0
            found_opening = False
            
            # Find the opening brace
            for j in range(i, len(lines)):
                if '{' in lines[j]:
                    found_opening = True
                    brace_count += lines[j].count('{')
                    brace_count -= lines[j].count('}')
                    break
            
            if not found_opening:
                i += 1
                continue
            
            # Find the closing brace
            j += 1
            while j < len(lines) and brace_count > 0:
                brace_count += lines[j].count('{')
                brace_count -= lines[j].count('}')
                j += 1
            
            if brace_count == 0:
                test_cases.append((start, j - 1))
            
        i += 1
    
    return test_cases

def test_case_uses_obsolete_patterns(lines, start, end):
    """Check if a test case uses data_source or istream_reader"""
    content = '\n'.join(lines[start:end+1])
    return 'data_source' in content or 'istream_reader' in content

def remove_obsolete_test_cases(content, file_path):
    """Remove test cases that use data_source or istream_reader"""
    lines = content.split('\n')
    test_cases = find_test_case_boundaries(lines)
    
    # Find test cases to remove (in reverse order to maintain indices)
    to_remove = []
    for start, end in reversed(test_cases):
        if test_case_uses_obsolete_patterns(lines, start, end):
            # Extract test case name for logging
            test_name = "unknown"
            for i in range(start, min(start + 3, len(lines))):
                match = re.search(r'TEST_CASE\("([^"]+)"', lines[i])
                if match:
                    test_name = match.group(1)
                    break
            
            to_remove.append((start, end, test_name))
    
    if not to_remove:
        return content, False
    
    # Remove the test cases
    modified_lines = lines[:]
    for start, end, test_name in to_remove:
        print(f"  Removing test case: '{test_name}' (lines {start+1}-{end+1})")
        # Remove lines from start to end (inclusive)
        for i in range(end, start - 1, -1):
            if i < len(modified_lines):
                del modified_lines[i]
    
    return '\n'.join(modified_lines), True

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 remove_obsolete_tests.py <kth-mono-path>")
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
    total_removed = 0
    
    for file_path in cpp_files:
        print(f"\nProcessing: {file_path}")
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Check if file contains obsolete patterns
            if 'data_source' not in content and 'istream_reader' not in content:
                continue
            
            new_content, modified = remove_obsolete_test_cases(content, file_path)
            
            if modified:
                # Create backup
                backup_path = file_path + '.backup_obsolete'
                with open(backup_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                
                # Write new content
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                
                total_modified += 1
                lines_removed = len(content.split('\n')) - len(new_content.split('\n'))
                total_removed += lines_removed
                print(f"  ✓ Modified {file_path} (backup: {backup_path})")
                print(f"    Removed {lines_removed} lines")
            else:
                print(f"  - No test cases to remove in {file_path}")
                
        except Exception as e:
            print(f"  ✗ Error processing {file_path}: {e}")
    
    print(f"\n=== Summary ===")
    print(f"Total files processed: {len(cpp_files)}")
    print(f"Files modified: {total_modified}")
    print(f"Total lines removed: {total_removed}")
    print(f"Backup files created with .backup_obsolete extension")

if __name__ == "__main__":
    main()
