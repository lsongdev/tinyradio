# tinyradio website

Buildless Preact + htm ESM website. `index.html` pins browser dependencies via
an import map; `style.css` extends https://lsong.org/css/stylesheet.css.

## Preview and checks

```sh
node --test www/test/*.test.js
# Authenticated gh is needed to mirror real release firmware:
python3 tools/build_website.py --output dist/www
python3 -m http.server 8080 --directory dist/www
```

Open http://localhost:8080. Serving `www/` directly also works, with an empty
firmware list until the build step populates it. No npm install or bundler is
needed. USB access needs a secure context (HTTPS or localhost), Web Serial,
a data cable, and a free serial port. Desktop Chrome/Edge are the recommended
browsers. The layout supports phones, but iOS Safari does not support Web
Serial; Android availability varies and no USB polyfill is included.

## GitHub Pages

Set repository Settings → Pages → Source to **GitHub Actions**. The Website
workflow publishes `dist/www` after website pushes, successful master Firmware
runs (after daily release publication), manual dispatch, and release edits.
It mirrors the latest eight compatible releases and checks SHA-256 against
release build metadata. Keeping the manifest and binaries on Pages avoids
GitHub release-download CORS and browser API rate limits. The browser also
checks image length/SHA-256 before writing and MD5 against flash after writing.

ATS Mini uses ESP32-S3 / 16 MiB; V5A uses ESP32 / 4 MiB. The flasher checks the
actual chip and flash capacity. Application updates write at 0x10000; complete
filled images write at 0 and overwrite settings/data. V5A users must back up
vendor firmware with esptool before installation (see hardware guide).

## Device console

`protocol.js` implements the current single-character console (`?`, `S`, encoder
focus and turn commands) without firmware changes. Requests are serialized;
timeout or malformed binary data closes the connection to avoid interpreting
an unfinished RGB888 frame as text. Screenshot payload size and exact end marker
are validated. Preview is a complete-frame transfer, not video; at 115200 baud
it can take 15–25 seconds and pauses the device UI. Continuous preview is opt-in,
does not queue overlapping captures, and pauses while the tab is hidden.

`firmware.js` loads pinned Espressif esptool-js only when flashing. Flashing and
the console own the port separately; reconnect after flashing because the USB
port identity can change. No serial data is uploaded. Browser dependencies and
the shared CSS are fetched from their public CDNs.

Actual hardware flashing, RF/audio behavior, and screenshots need a connected
receiver for verification. Automated tests simulate serial streams only.
