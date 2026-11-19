"""Unit tests for tools/distress/import_em.py.

Run from tools/distress: python3 -m unittest discover -s tests
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import import_em  # noqa: E402


class BaseModelTest(unittest.TestCase):
    def test_a_config_without_a_suffix_is_its_model(self):
        self.assertEqual(import_em.base_model("parakeet-0.6b-v2-int8"), "parakeet-0.6b-v2-int8")

    def test_the_window_suffix_is_removed(self):
        self.assertEqual(import_em.base_model("parakeet-0.6b-v2-int8-w5"), "parakeet-0.6b-v2-int8")

    def test_the_thread_suffix_is_removed(self):
        self.assertEqual(import_em.base_model("parakeet-0.6b-v2-int8-t2"), "parakeet-0.6b-v2-int8")

    def test_a_model_name_with_digits_keeps_them(self):
        self.assertEqual(import_em.base_model("parakeet-110m-int8"), "parakeet-110m-int8")


if __name__ == "__main__":
    unittest.main()
