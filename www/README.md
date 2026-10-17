# tinyradio website

Static Preact + htm ESM website. Deploy `www/` directly: no generated files,
package installation, bundler, or website build step. Browser dependencies are
pinned in `index.html`; `style.css` extends https://lsong.org/css/stylesheet.css.

For local preview, serve this directory with any static HTTP server, e.g.:

```sh
python3 -m http.server 8080 --directory www
```

The Website workflow uploads `www/` unchanged to GitHub Pages after website
pushes or manual dispatch. Releases do not require a site redeployment.

## Releases and flashing

`releases.js` fetches the public GitHub Releases API when the page opens,
including prereleases. It selects board-specific assets and uses the SHA-256
`digest` and size supplied by GitHub. Assets without a digest are excluded.
Unauthenticated GitHub API rate limits apply; no token is embedded in the site.

The browser tries the release asset API with `Accept: application/octet-stream`.
GitHub's redirect to release storage can be blocked by browser CORS rules.
In that case, use the download link, then select that .bin with the local file
picker. Both automatic downloads and local files must match the selected
release's SHA-256 and size before the flasher opens the port. No proxy or
server-side firmware mirror is used.

`firmware.js` imports Espressif's browser ESM bundle directly. It checks chip
and flash capacity before writing. ATS Mini uses ESP32-S3 / 16 MiB; V5A uses
ESP32 / 4 MiB. Updates write at 0x10000; complete images write at 0 and overwrite
settings/data. Back up V5A vendor firmware before installation.

USB access requires HTTPS or localhost, Web Serial, a data cable, and an
available port. Desktop Chrome/Edge are recommended. iOS Safari has no Web
Serial; Android support varies. Reconnect after flashing because the port
identity can change.

## Device console

`protocol.js` implements the single-character console and framed RGB888 screen
transfer. Requests are serialized. Timeout or malformed screen data closes the
connection to avoid reusing a partial binary stream. Complete screen transfers
can take 15–25 seconds at 115200 baud and briefly pause the device UI.
Continuous preview is opt-in and pauses while the tab is hidden.

No serial data or selected firmware files are uploaded. RF behavior and actual
USB flashing require a connected receiver. Run the optional simulated checks
with `node --test www/test/*.test.js`; they are not a build step.
