"""
Structure Pipeline P2 — author tests (offline; a fake LLM, no API calls).

Run: python tools/structure_pipeline/test_author.py
"""

import copy
import json
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from structure_pipeline import (  # noqa: E402
    author_spec, build_system_prompt, build_user_prompt, extract_json, load_canon,
)

# A minimal VALID BuildingSpec (single room, one front door at the new door canon: w1 h2).
GOOD = {
    "kind": "building", "name": "hut", "style": "medieval", "function": "house",
    "palette": {"wall": "Stone", "floor": "Wood", "roof": "Wood"},
    "footprint": [8, 8],
    "stories": [{
        "height": 4,
        "rooms": [{"id": "r", "rect": [0, 0, 8, 8], "purpose": "main", "floor_mat": "Wood"}],
        "portals": [{"between": ["exterior", "r"], "pos": [3, 0], "width": 1, "height": 2,
                     "kind": "door", "door": {"lockable": False, "key": "", "swing": 90}}],
        "stairs": [], "fixtures": [],
    }],
    "roof": {"style": "pitched", "mat": "Wood"},
}


def _broken_low_ceiling():
    d = copy.deepcopy(GOOD)
    d["stories"][0]["height"] = 1  # -> CEILING_TOO_LOW
    return d


class FakeLLM:
    """Returns queued responses; records the prompts it was called with."""
    def __init__(self, responses):
        self.responses = list(responses)
        self.calls = []

    def __call__(self, system, user):
        self.calls.append((system, user))
        return self.responses.pop(0)


class ExtractJsonTests(unittest.TestCase):
    def test_fenced(self):
        self.assertEqual(extract_json('```json\n{"a": 1}\n```'), {"a": 1})

    def test_prose_wrapped(self):
        self.assertEqual(extract_json('Sure! Here:\n{"a": 2}\nDone.'), {"a": 2})

    def test_nested_and_string_braces(self):
        obj = {"a": {"b": 3}, "s": "has } brace { inside"}
        self.assertEqual(extract_json("noise " + json.dumps(obj) + " trailing"), obj)

    def test_no_json_raises(self):
        with self.assertRaises(ValueError):
            extract_json("no object here")


class PromptTests(unittest.TestCase):
    def test_system_prompt_has_canon_and_schema(self):
        p = build_system_prompt(load_canon())
        self.assertIn("1.75", p)            # character height
        self.assertIn("BuildingSpec", p)
        self.assertIn("footprint", p)
        self.assertIn("exterior", p)        # reachability rule
        self.assertIn("Stone", p)           # materials list

    def test_user_prompt_includes_params(self):
        u = build_user_prompt("a tiny shop", function="shop", footprint=[10, 12], stories=2)
        self.assertIn("a tiny shop", u)
        self.assertIn("shop", u)
        self.assertIn("10 x 12", u)


class AuthorLoopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canon = load_canon()

    def test_valid_first_try(self):
        llm = FakeLLM([json.dumps(GOOD)])
        res = author_spec("a hut", llm=llm, canon=self.canon)
        self.assertTrue(res.ok, res.report.summary())
        self.assertEqual(res.rounds, 0)
        self.assertEqual(len(llm.calls), 1)

    def test_repairs_once(self):
        llm = FakeLLM([json.dumps(_broken_low_ceiling()), "```json\n" + json.dumps(GOOD) + "\n```"])
        res = author_spec("a hut", llm=llm, canon=self.canon)
        self.assertTrue(res.ok, res.report.summary())
        self.assertEqual(res.rounds, 1)
        # the repair prompt must feed back the actual validation error
        self.assertIn("CEILING_TOO_LOW", llm.calls[1][1])

    def test_exhausts_repair_budget(self):
        broken = json.dumps(_broken_low_ceiling())
        llm = FakeLLM([broken] * 5)
        res = author_spec("a hut", llm=llm, canon=self.canon, max_repair=2)
        self.assertFalse(res.ok)
        self.assertEqual(res.rounds, 2)
        self.assertEqual(len(llm.calls), 3)  # initial + 2 repairs


if __name__ == "__main__":
    unittest.main(verbosity=2)
