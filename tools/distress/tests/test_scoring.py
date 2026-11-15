"""Unit tests for the scoring rules in tools/distress/scoring.py.

Run from tools/distress: python3 -m unittest discover -s tests
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import scoring  # noqa: E402

PHRASES = [("mayday",), ("taking", "on", "water"), ("fire",)]


class NormalizeTest(unittest.TestCase):
    def test_case_and_punctuation_are_ignored(self):
        self.assertEqual(scoring.normalize("Roger, Captain. Over?"), ["roger", "captain", "over"])

    def test_hyphen_is_a_space(self):
        self.assertEqual(scoring.normalize("forty-five"), ["forty", "five"])

    def test_apostrophe_in_a_word_is_kept(self):
        self.assertEqual(scoring.normalize("We're 'sinking'"), ["we're", "sinking"])

    def test_unintelligible_tag_is_removed(self):
        self.assertEqual(scoring.normalize("the engine [unintelligible]."), ["the", "engine"])

    def test_fillers_are_removed(self):
        self.assertEqual(scoring.normalize("we're uh about um eight"), ["we're", "about", "eight"])

    def test_oh_and_o_are_zero(self):
        self.assertEqual(scoring.normalize("one two four oh eight o"), "one two four zero eight zero".split())

    def test_integer_digits_are_words(self):
        self.assertEqual(scoring.normalize("channel 16"), ["channel", "sixteen"])
        self.assertEqual(scoring.normalize("124"), ["one", "hundred", "twenty", "four"])
        self.assertEqual(scoring.normalize("12407"), "twelve thousand four hundred seven".split())

    def test_leading_zeros_are_said_as_zero(self):
        self.assertEqual(scoring.normalize("0451"), "zero four hundred fifty one".split())
        self.assertEqual(scoring.normalize("0"), ["zero"])

    def test_decimal_digits_are_said_one_by_one(self):
        self.assertEqual(scoring.normalize("16.75"), "sixteen point seven five".split())

    def test_digits_and_words_normalize_the_same(self):
        self.assertEqual(scoring.normalize("46 16.75"), scoring.normalize("forty six sixteen point seven five"))


class ParseReferenceTest(unittest.TestCase):
    def _write(self, text):
        f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
        f.write(text)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_each_word_gets_the_speaker_of_its_line(self):
        path = self._write("# id: 1\n# name: x\n# status: checked\n\nVESSEL: Mayday, sinking.\nUSCG: Position, over?\n")
        ref = scoring.parse_reference(path)
        self.assertEqual(ref.clip, "1-x")
        self.assertEqual(ref.words, ["mayday", "sinking", "position", "over"])
        self.assertEqual(ref.speakers, ["VESSEL", "VESSEL", "USCG", "USCG"])
        self.assertFalse(ref.wer_excluded)

    def test_wer_exclude_header_is_read(self):
        path = self._write("# id: 1\n# name: x\n# wer: exclude\n\nVESSEL: Mayday.\n")
        self.assertTrue(scoring.parse_reference(path).wer_excluded)

    def test_line_without_a_speaker_label_is_an_error(self):
        path = self._write("# id: 1\n# name: x\n\nMayday.\n")
        with self.assertRaises(ValueError):
            scoring.parse_reference(path)


class ErrorCountsTest(unittest.TestCase):
    def test_substitution_and_deletion_count_for_the_reference_speaker(self):
        ref = ["mayday", "sinking", "position", "over"]
        speakers = ["VESSEL", "VESSEL", "USCG", "USCG"]
        counts = scoring.error_counts(ref, speakers, ["mayday", "thinking", "position"])
        self.assertEqual(counts["VESSEL"], {"words": 2, "sub": 1, "del": 0, "ins": 0})
        self.assertEqual(counts["USCG"], {"words": 2, "sub": 0, "del": 1, "ins": 0})

    def test_insertion_counts_for_the_speaker_of_the_reference_word_before_it(self):
        counts = scoring.error_counts(["mayday", "over"], ["VESSEL", "USCG"], ["mayday", "extra", "over"])
        self.assertEqual(counts["VESSEL"]["ins"], 1)
        self.assertEqual(counts["USCG"]["ins"], 0)

    def test_insertion_before_the_first_word_counts_for_the_first_speaker(self):
        counts = scoring.error_counts(["over"], ["USCG"], ["extra", "over"])
        self.assertEqual(counts["USCG"]["ins"], 1)

    def test_ship_side_wer_leaves_out_the_coast_guard(self):
        counts = scoring.error_counts(["help", "over"], ["VESSEL", "USCG"], ["help", "clover"])
        self.assertEqual(scoring.wer(counts, scoring.SHIP_SIDE), 0.0)
        self.assertEqual(scoring.wer(counts, scoring.SPEAKERS), 0.5)

    def test_wer_of_a_group_without_words_is_none(self):
        counts = scoring.error_counts(["over"], ["USCG"], ["over"])
        self.assertIsNone(scoring.wer(counts, scoring.SHIP_SIDE))


class HypSpeakersTest(unittest.TestCase):
    def test_model_words_get_the_speaker_of_the_aligned_reference_word(self):
        speakers = scoring.hyp_speakers(["mayday", "over"], ["VESSEL", "USCG"], ["mayday", "uhh", "clover"])
        self.assertEqual(speakers, ["VESSEL", "VESSEL", "USCG"])


class FindPhrasesTest(unittest.TestCase):
    def test_multi_word_phrase_matches_a_run_of_whole_words(self):
        self.assertEqual(scoring.find_phrases("we are taking on water".split(), PHRASES), ["taking on water"])
        self.assertEqual(scoring.find_phrases("taking water on".split(), PHRASES), [])

    def test_phrase_does_not_match_part_of_a_word(self):
        self.assertEqual(scoring.find_phrases(["firefighter"], PHRASES), [])

    def test_only_mayday_allows_one_letter_difference(self):
        self.assertEqual(scoring.find_phrases(["maydey"], PHRASES), ["mayday"])
        self.assertEqual(scoring.find_phrases(["five"], PHRASES), [])

    def test_mayday_fuzzy_match_needs_the_same_first_letter(self):
        self.assertEqual(scoring.find_phrases(["payday"], PHRASES), [])

    def test_mayday_split_in_two_words_matches(self):
        self.assertEqual(scoring.find_phrases(["may", "day"], PHRASES), ["mayday"])

    def test_mayday_is_not_found_when_it_is_not_in_the_list(self):
        self.assertEqual(scoring.find_phrases(["mayday"], [("fire",)]), [])

    def test_allowed_mask_rejects_a_match_on_coast_guard_words(self):
        words = "vessel taking on water".split()
        self.assertEqual(scoring.find_phrases(words, PHRASES, allowed=[False] * 4), [])
        self.assertEqual(scoring.find_phrases(words, PHRASES, allowed=[False, True, True, False]), [])
        self.assertEqual(scoring.find_phrases(words, PHRASES, allowed=[False, True, True, True]), ["taking on water"])


class PhraseListTest(unittest.TestCase):
    def test_committed_phrase_list_loads_normalized(self):
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        phrases = scoring.load_phrases(os.path.join(here, "phrases.txt"))
        self.assertIn(("mayday",), phrases)
        self.assertIn(("taking", "on", "water"), phrases)
        self.assertIn(("s", "zero", "s"), phrases)


if __name__ == "__main__":
    unittest.main()
