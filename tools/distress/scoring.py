"""Scoring rules for the distress call evaluation (issue #142).

The rules are in tools/distress/README.md, section "Reference transcripts".
This module has no dependencies outside the Python standard library.
"""

import re
from dataclasses import dataclass

SPEAKERS = ("VESSEL", "USCG", "OTHER")
SHIP_SIDE = frozenset({"VESSEL", "OTHER"})
FILLERS = frozenset({"uh", "um", "uhm", "er", "erm", "ah", "hmm", "mm"})
UNINTELLIGIBLE = "[unintelligible]"
MAYDAY = "mayday"

_ONES = ("zero one two three four five six seven eight nine ten eleven twelve thirteen "
         "fourteen fifteen sixteen seventeen eighteen nineteen").split()
_TENS = "_ _ twenty thirty forty fifty sixty seventy eighty ninety".split()
_SCALES = ((10**9, "billion"), (10**6, "million"), (1000, "thousand"), (100, "hundred"))
_NUMBER = re.compile(r"\d+(?:\.\d+)?")


def _int_words(n):
    """Return the words of a non-negative integer, for example 451 -> four hundred fifty one."""
    if n < 20:
        return [_ONES[n]]
    if n < 100:
        return [_TENS[n // 10]] + ([_ONES[n % 10]] if n % 10 else [])
    for value, name in _SCALES:
        if n >= value:
            rest = _int_words(n % value) if n % value else []
            return _int_words(n // value) + [name] + rest
    raise ValueError(n)


def number_words(token):
    """Return the words of a digit string.

    Leading zeros are said as "zero" ("0451" -> zero four hundred fifty one).
    The digits after a decimal point are said one by one ("16.75" -> sixteen point seven five).
    """
    whole, _, fraction = token.partition(".")
    words = ["zero"] * (len(whole) - len(whole.lstrip("0")))
    if whole.lstrip("0"):
        words += _int_words(int(whole))
    elif not words:
        words = ["zero"]
    if fraction:
        words += ["point"] + [_ONES[int(d)] for d in fraction]
    return words


def normalize(text):
    """Normalize a reference or a model output to a list of words.

    The rules: lower case, no punctuation, a hyphen is a space, no
    [unintelligible] tags, no filler words, digits as words, and "oh" or "o" as "zero".
    """
    text = text.lower().replace(UNINTELLIGIBLE, " ")
    text = _NUMBER.sub(lambda m: " " + " ".join(number_words(m.group())) + " ", text)
    text = text.replace("-", " ")
    text = re.sub(r"[^a-z' ]+", " ", text)
    words = []
    for word in text.split():
        word = word.strip("'")
        if not word or word in FILLERS:
            continue
        words.append("zero" if word in ("oh", "o") else word)
    return words


@dataclass
class Reference:
    """A parsed reference transcript."""

    clip: str
    status: str
    wer_excluded: bool
    words: list
    speakers: list


def parse_reference(path):
    """Read a reference transcript file. Each word gets the speaker label of its line."""
    header, words, speakers = {}, [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("#"):
                key, _, value = line[1:].partition(":")
                header[key.strip()] = value.strip()
                continue
            if not line:
                continue
            label, sep, text = line.partition(":")
            if not sep or label not in SPEAKERS:
                raise ValueError(f"{path}: line without a speaker label: {line!r}")
            line_words = normalize(text)
            words += line_words
            speakers += [label] * len(line_words)
    clip = f"{header.get('id')}-{header.get('name')}"
    return Reference(clip, header.get("status", ""), header.get("wer") == "exclude", words, speakers)


def align(ref, hyp):
    """Align two word lists with the minimum edit distance.

    Return a list of (op, ref_index, hyp_index). The op is "match", "sub",
    "del" (a reference word is missing), or "ins" (an extra model word). The
    index of the side that has no word is None.
    """
    n, m = len(ref), len(hyp)
    cost = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n + 1):
        cost[i][0] = i
    for j in range(m + 1):
        cost[0][j] = j
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            diagonal = cost[i - 1][j - 1] + (ref[i - 1] != hyp[j - 1])
            cost[i][j] = min(diagonal, cost[i - 1][j] + 1, cost[i][j - 1] + 1)
    ops, i, j = [], n, m
    while i or j:
        if i and j and cost[i][j] == cost[i - 1][j - 1] + (ref[i - 1] != hyp[j - 1]):
            ops.append(("match" if ref[i - 1] == hyp[j - 1] else "sub", i - 1, j - 1))
            i, j = i - 1, j - 1
        elif i and cost[i][j] == cost[i - 1][j] + 1:
            ops.append(("del", i - 1, None))
            i -= 1
        else:
            ops.append(("ins", None, j - 1))
            j -= 1
    return ops[::-1]


def _insertion_speakers(ops, speakers):
    """Give each op a speaker. An insertion gets the speaker of the reference word before it.

    An insertion before the first reference word gets the speaker of the first reference word.
    """
    first = speakers[0] if speakers else None
    result, previous = [], None
    for op, i, _ in ops:
        if i is not None:
            previous = speakers[i]
            result.append(previous)
        else:
            result.append(previous or first)
    return result


def error_counts(ref_words, speakers, hyp_words):
    """Count the reference words and the errors per speaker.

    Return {speaker: {"words": n, "sub": n, "del": n, "ins": n}}.
    """
    counts = {s: {"words": 0, "sub": 0, "del": 0, "ins": 0} for s in SPEAKERS}
    for s in speakers:
        counts[s]["words"] += 1
    ops = align(ref_words, hyp_words)
    for (op, _, _), speaker in zip(ops, _insertion_speakers(ops, speakers)):
        if op != "match" and speaker is not None:
            counts[speaker][op] += 1
    return counts


def wer(counts, group):
    """Return the WER over the speakers in group, or None if the group has no reference words."""
    words = sum(counts[s]["words"] for s in group)
    errors = sum(counts[s][k] for s in group for k in ("sub", "del", "ins"))
    return errors / words if words else None


def hyp_speakers(ref_words, speakers, hyp_words):
    """Give each model output word the speaker of the reference word that it aligns with."""
    ops = align(ref_words, hyp_words)
    result = [None] * len(hyp_words)
    for (op, _, j), speaker in zip(ops, _insertion_speakers(ops, speakers)):
        if j is not None:
            result[j] = speaker
    return result


def load_phrases(path):
    """Read the phrase list. Each phrase is normalized like the model output."""
    phrases = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                phrases.append(tuple(normalize(line)))
    return phrases


def levenshtein(a, b):
    """Return the edit distance between two strings."""
    row = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        previous, row[0] = row[0], i
        for j, cb in enumerate(b, 1):
            previous, row[j] = row[j], min(row[j] + 1, row[j - 1] + 1, previous + (ca != cb))
    return row[-1]


def is_mayday(word):
    """True if a word is "mayday" with at most one letter different and the same first letter.

    This is the rule of the flight matcher for one keyword.
    """
    return bool(word) and word[0] == MAYDAY[0] and levenshtein(word, MAYDAY) <= 1


def find_phrases(words, phrases, allowed=None):
    """Find the distress phrases in a word list.

    Return the sorted set of the phrases found, each as a string. A phrase
    matches a run of whole words. "mayday" also matches one word that
    is_mayday() accepts, or two words that are next to each other and make
    "mayday" when joined ("may day"). If allowed is given, a match counts
    only if allowed[k] is True for each word in the match.
    """
    ok = (lambda k: allowed[k]) if allowed is not None else (lambda k: True)
    found = set()
    for k in range(len(words)):
        for phrase in phrases:
            end = k + len(phrase)
            if tuple(words[k:end]) == phrase and all(ok(x) for x in range(k, end)):
                found.add(" ".join(phrase))
        if (MAYDAY,) in phrases:
            if is_mayday(words[k]) and ok(k):
                found.add(MAYDAY)
            if k + 1 < len(words) and is_mayday(words[k] + words[k + 1]) and ok(k) and ok(k + 1):
                found.add(MAYDAY)
    return sorted(found)
