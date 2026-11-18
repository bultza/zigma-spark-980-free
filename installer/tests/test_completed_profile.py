"""Regression: an earlier SSH fallback must not hide a verified Full retry."""
import ast
from pathlib import Path
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
tree = ast.parse((ROOT/'computer/wizard.py').read_text())
robot = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == 'Robot')
namespace = {}
exec(compile(ast.Module(body=[robot], type_ignores=[]), 'wizard-Robot', 'exec'), namespace)
Robot = namespace['Robot']

class CompletedProfile(unittest.TestCase):
    def check(self, records, installed=True, expected=False):
        robot = Robot('192.0.2.1', None, Path('/tmp'))
        robot.command = Mock(return_value='\n'.join(records))
        robot.installed = Mock(return_value=installed)
        self.assertEqual(robot.completed_profile('target'), expected)
        if 'target\nfull\nfull' in '\n'.join(records):
            robot.installed.assert_called_once_with('target')
        else:
            robot.installed.assert_not_called()

    def test_fallback_then_full(self):
        self.check(['target','full','ssh','target','full','full'], expected='full')

    def test_full_then_old_fallback(self):
        self.check(['target','full','full','target','full','ssh'], expected='full')

    def test_full_requires_actual_installed_marker(self):
        self.check(['target','full','ssh','target','full','full'], installed=False)

    def test_only_fallback(self):
        self.check(['target','full','ssh'], expected='ssh')

    def test_other_bundle_and_truncated_record(self):
        self.check(['other','full','full','target'])

    def test_unrecognized_profile(self):
        self.check(['target','unknown','full'])

    def test_pending_or_remote_failure(self):
        robot = Robot('192.0.2.1',None,Path('/tmp'))
        robot.command = Mock(side_effect=RuntimeError('pending'))
        robot.installed = Mock()
        self.assertFalse(robot.completed_profile('target'))
        robot.installed.assert_not_called()
