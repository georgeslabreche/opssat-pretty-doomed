# Public landing page

`index.html` is a self-contained public landing page for the experiment (a general-audience explainer of the milestone: the first voice command of a satellite). It has no build step and no external dependencies; `postcard.png` is the one image it uses.

Styling follows the [tanagraspace/ccsds124](https://github.com/tanagraspace/ccsds124) landing page: a warm-tan palette with a coral accent, a hero with an animated satellite, and a responsive card grid, all in an embedded stylesheet.

## Serving it with GitHub Pages

The page is plain static HTML, so GitHub Pages serves it as-is (no Jekyll needed). Options:

- Configure Pages to build from the `main` branch `/docs` folder; the landing page is then at `.../docs/site/`.
- Or publish just this folder with a GitHub Actions Pages workflow.
- Or move `index.html` and `postcard.png` up to `docs/` if you prefer the landing page at the Pages root.

To preview locally, open `index.html` in a browser, or run `python3 -m http.server` from this directory.
