#!/usr/bin/env python3
"""Behavioral inventory and compilation-shape controls for the real imports.

--source may name an unmodified checkout to reproduce the layout regression.
The golden inventory is intentionally kept in this checkout, outside that source.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import unittest

from pack_imported_vectors import manifest, original_groups, packed_groups

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1] / 'src/domain/test/chain'


class ImportedVectors(unittest.TestCase):
    def test_exact_adapted_inventory(self):
        groups = (packed_groups(SOURCE) if list(SOURCE.glob('imported_*_data.cpp'))
                  else original_groups(SOURCE))
        key = lambda g: (g['kind'], g['source'])
        expected = json.loads((HERE / 'imported_vectors_inventory.json').read_text())
        self.assertEqual(sorted(manifest(groups), key=key), sorted(expected, key=key))
        counts = Counter()
        for group in groups:
            counts[group['kind']] += len(group['rows'])
        self.assertEqual(counts, {'bchn': 1758, 'vmb': 89821})

    def test_bounded_test_registration_and_lightweight_data(self):
        files = [*SOURCE.glob('script_vm_auto_*.cpp'), *SOURCE.glob('vmb_auto_*.cpp'),
                 *SOURCE.glob('imported_*_data.cpp')]
        driver = SOURCE / 'imported_vectors.cpp'
        if driver.exists():
            files.append(driver)
        registrations = sum(sum(line.startswith('TEST_CASE(') for line in path.read_text().splitlines())
                            for path in files)
        self.assertEqual(registrations, 13,
                         'Imported cases must be data traversed by 13 suite tests')
        data = list(SOURCE.glob('imported_*_data.cpp'))
        self.assertEqual(len(data), 13)
        for path in data:
            text = path.read_text()
            self.assertFalse(any(line.startswith('TEST_CASE(') for line in text.splitlines()))
            self.assertNotIn('test_helpers.hpp', text)
            self.assertNotIn('vmb_tests.hpp', text)
        includes = Counter(fragment for path in data
                           for fragment in re.findall(r'^#include "((?:script_vm_auto_|vmb_auto_).*?\.cpp)"$', path.read_text(), re.M))
        fragments = {path.name for pattern in ('script_vm_auto_*.cpp', 'vmb_auto_*.cpp')
                     for path in SOURCE.glob(pattern)}
        self.assertEqual(len(fragments), 524)
        self.assertEqual(set(includes), fragments)
        self.assertTrue(all(count == 1 for count in includes.values()))
        for path in [*data, *(SOURCE / name for name in fragments)]:
            self.assertLess(path.stat().st_size, 64 * 1024 * 1024)
        self.assertNotIn('inline\nvoid run_vmb_test', (SOURCE / 'vmb_tests.hpp').read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=SOURCE)
    args, remaining = parser.parse_known_args()
    SOURCE = args.source
    unittest.main(argv=['test_imported_vectors', *remaining])
