"""Output directories of the distress evaluation scripts.

The scripts are in tools/distress. Their output (model outputs, scores, and reports) is
documentation, so it is in pretty-doomed/docs/distress, not with the source code.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.normpath(os.path.join(HERE, "..", "..", "pretty-doomed", "docs", "distress"))
RESULTS = os.path.join(DOCS, "results")
REPORTS = os.path.join(DOCS, "reports")
