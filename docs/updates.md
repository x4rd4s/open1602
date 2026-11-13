# Updates

Open1602 can update itself from its GitHub releases. The main menu has a button "Updates" at the
bottom right. It opens a screen that shows the installed and the newest version, checks for a new
one, installs it and starts the new version. A switch on the same screen turns the check at every
start on or off.

## For players

- **The first start** asks once whether Open1602 should look for new versions at every start. The
  answer is kept in `settings.cfg` as `update_check = on` or `off`. The switch on the screen
  "Updates" changes it later.
- **The check at the start** runs in the background. Without a connection it stays silent. When a
  newer version is out, the main menu asks whether to install it, with three answers:
  - "Later" asks again at the next start.
  - "Install" opens the screen "Updates" and starts the download.
  - "Skip this version" does not ask again until a later version is out (`skipped_update`).

  The question only appears in the main menu, never during a game.
- **Installing** downloads the package of this platform, checks its size and SHA-256, and puts the
  new files in place. The files of the version before are kept. The button "Restart" then starts
  the new version.
- **Back to the previous version:** the screen "Updates" offers the version the last update
  replaced, until the next update.
- **Never touched:** savegames, `settings.cfg`, the mods folder and everything else in the user
  data folder. The original Anno 1602 data is never part of an update either.

### Which copies update themselves

Only the packages the release workflow builds update themselves. They carry a file `release.toml`
that names their platform:

| Platform | Where `release.toml` lies | What is replaced |
|---|---|---|
| Windows | beside `open1602.exe` | the files of the package, one by one |
| Linux | `share/open1602/release.toml` | the files of the package, one by one |
| macOS | `Open1602.app/Contents/Resources/` | the whole app bundle |

The other copies are only told that a new version is out, with a link to the download page:

- A build of your own has no `release.toml`.
- A distribution's package is updated by the distribution.
- A copy in a folder Open1602 may not write to, such as `C:\Program Files`, cannot be updated
  this way.
- On macOS, an app started straight from the Downloads folder runs from a read-only copy
  (App Translocation). Move `Open1602.app` to another folder, for example Applications, and
  start it again.

On Windows and Linux the update needs a folder Open1602 may write to. It works in a work folder
`.update` beside the program and renames the files into place. On macOS the work folder is
`.Open1602-update` beside the app bundle.

### HTTPS

Open1602 brings no TLS library of its own; it uses what the system has. On Windows that is WinHTTP,
with the system's certificates and proxy settings. On Linux and macOS it is the system's libcurl
(`libcurl.so.4` or `libcurl-gnutls.so.4`, and `/usr/lib/libcurl.4.dylib` on macOS), loaded only
when an update check runs. Without libcurl the screen says so, and the game runs as before.

## For maintainers

### What a release carries

Every release that the release workflow publishes has two files besides the packages:

- `update.toml` holds the version, the address of the release notes, and for each platform the
  address, size and SHA-256 of its package:

  ```toml
  format = 1
  version = "0.6.0"
  notes = "https://github.com/x4rd4s/open1602/releases/tag/v0.6.0"

  [windows-x64]
  url = "https://github.com/x4rd4s/open1602/releases/download/v0.6.0/Open1602-0.6.0-windows-x64.zip"
  size = 18522112
  sha256 = "…"
  ```

- `update.toml.sig` holds the Ed25519 signature over the bytes of `update.toml`, as 128
  hexadecimal digits.

Open1602 fetches both from `releases/latest/download/`. That address is not rate limited like the
GitHub API, and it skips pre-releases, so a pre-release reaches only the players who download it
themselves. Open1602 believes `update.toml` only when the signature matches the release key built
into the program (`kReleaseKey` in `engine/app/src/updater.cpp`). A file of a later `format` makes
the screen ask the player to update by hand.

`.github/scripts/update-manifest.sh` writes and signs both files in the job "Publish the release".
A run with `macos-only` downloads the packages already published and writes both files anew, so
that the macOS package is listed too.

### The release key

The private half of the release key is the repository secret `UPDATE_SIGNING_KEY`, an Ed25519 key
in PEM, as OpenSSL writes it:

```sh
openssl genpkey -algorithm ed25519 -out update-signing-key.pem
# the public half, as kReleaseKey wants it:
openssl pkey -in update-signing-key.pem -pubout -outform DER | tail -c 32 | od -An -tx1 -v | tr -d ' \n'
```

A release fails when the secret is missing, or when its public half is not `kReleaseKey`. Keep a
copy of the private key offline. Without it, no copy of Open1602 that is already out believes a
new release.

To change the key, add the new public half to `release_keys()` beside the old one and publish a
release signed with the old key. Once players have that version, sign with the new key and take
the old one out.

### What is not covered

- The packages are not code-signed yet. Windows SmartScreen and macOS Gatekeeper still warn on
  the first start of a downloaded package. An update downloaded by Open1602 itself is not marked
  as downloaded from the internet.
- The first version with the updater has to be downloaded by hand once.
