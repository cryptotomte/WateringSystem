# firmware/web — frontend served from littlefs (feature 010, PR-10)

Source of truth for the device dashboard. These files are **adapted copies** of the frozen Arduino frontend
in the repo-root `data/` (which stays read-only reference — never edit `data/`). The copies are adapted to
the frozen `/api/v1/` contract and vendored for offline operation.

At build time the ESP-IDF build gzips everything here (see `firmware/tools/gzip_assets.py` and
`firmware/main/CMakeLists.txt`) and packs it into the littlefs `storage` image, so the device serves e.g.
`/index.html` from `/storage/index.html.gz` with `Content-Encoding: gzip`.

## Files

- `index.html` — copy of `data/index.html`, CDN `<script>/<link>` references replaced with local `vendor/…`.
- `script.js` — copy of `data/script.js`, API layer adapted to `/api/v1/`.
- `styles.css` — copy of `data/styles.css` (custom component styles; loaded after `vendor/tailwind.css`).
- `favicon.ico` — copy of `data/favicon.ico`.
- `vendor/` — third-party libs vendored locally (no client-side internet dependency, per the PR-10 clarify
  decision — the greenhouse client may reach the device on the LAN but have no internet):
  - `chart.min.js` — **Chart.js v4.4.3** UMD build, from `https://cdn.jsdelivr.net/npm/chart.js@4.4.3/dist/chart.umd.min.js`.
  - `chartjs-adapter-date-fns.min.js` — **chartjs-adapter-date-fns v3.0.0** bundle (includes date-fns), from
    `https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js`.
  - `tailwind.css` — **Tailwind CSS v3.4.17**, a PURGED build (only the utility classes used by the markup),
    with `darkMode: 'class'` and `theme.extend.colors` = `brand-accent #3b82f6` / `brand-accent-hover #2563eb`
    / `brand-secondary #4b5563` / `brand-secondary-hover #374151`.

## Regenerating `vendor/tailwind.css` (manual step — NOT a CI dependency)

The ESP-IDF/CI build only gzips the committed files; it does not run Tailwind. Regenerate the purged CSS only
when the markup's class usage changes:

```sh
# from firmware/web/
cat > /tmp/tw.config.js <<'CFG'
module.exports = {
  darkMode: 'class',
  content: ['index.html', 'script.js'],
  theme: { extend: { colors: {
    'brand-accent': '#3b82f6', 'brand-accent-hover': '#2563eb',
    'brand-secondary': '#4b5563', 'brand-secondary-hover': '#374151',
  } } },
};
CFG
printf '@tailwind base;\n@tailwind components;\n@tailwind utilities;\n' > /tmp/tw.input.css
npx --yes tailwindcss@3.4.17 -c /tmp/tw.config.js -i /tmp/tw.input.css -o vendor/tailwind.css --minify
```

Bumping any vendored lib: replace the file, update the pinned version here, and re-run a HIL smoke.
