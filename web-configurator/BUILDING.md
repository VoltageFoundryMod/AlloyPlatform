# Building the Alloy Controller

One build, three shells. `make web` produces the only bundle there is; the PWA
serves it from a web server, and Capacitor wraps that same `dist/` for iOS and
Android. There is no second codebase — the only thing that differs at runtime is
which Bluetooth implementation [`src/lib/bleLink.ts`](src/lib/bleLink.ts) picks.

| Target  | Command                       | Needs                          |
| ------- | ----------------------------- | ------------------------------ |
| Web/PWA | `make web`                    | Node 18+                       |
| Android | `make app-install`            | Node, JDK 17+, Android SDK 36  |
| iOS     | `make app-ios`                | macOS, Xcode, Apple Developer  |
| Publish | push to `main`                | nothing — CI does it           |

All commands run from the **repository root** unless stated otherwise.

---

## Web / PWA

```bash
make web            # production build → web-configurator/dist/
make web-dev        # dev server on localhost:5173
make web-host       # same over HTTPS on the LAN, for phone testing
make web-check      # svelte-check + tsc
```

`dist/` is a self-contained static site. Serve it from any web server over
**HTTPS** — Web Bluetooth, Web MIDI and Web Serial are secure-context APIs, and
on a plain-HTTP origin the browser does not define them at all, so every
feature-detect reports "not supported" on a browser that supports them fine.
`localhost` counts as secure; a LAN IP does not, which is what `make web-host`
exists for (it serves over a self-signed certificate — accept the warning once).

Offline works automatically: the service worker precaches the whole build on
first visit. The **Install** button appears when the browser offers it, and
gives the page its own window. Neither needs a store.

⚠️ The service worker is **disabled in `make web-dev`** on purpose, so an edit
never gets served from a stale cache. To test offline behaviour, build and use
`npm run preview`.

---

## Android

### One-time toolchain setup

Nothing below is installed by this repo. Run it once.

**1. JDK 17 or newer.** AGP 8.13 refuses anything older; a JDK 8 on `PATH` is
the most common cause of an unexplained Gradle failure here.

```powershell
winget install EclipseAdoptium.Temurin.21.JDK
```

**2. Android command-line tools.** There is no winget package for these — only
full Android Studio — so download them directly. The SDK does not need Studio.

```powershell
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
New-Item -ItemType Directory -Force "$sdk\cmdline-tools" | Out-Null

Invoke-WebRequest `
  "https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip" `
  -OutFile "$env:TEMP\cmdline-tools.zip"

Expand-Archive "$env:TEMP\cmdline-tools.zip" -DestinationPath "$sdk\cmdline-tools" -Force
Rename-Item "$sdk\cmdline-tools\cmdline-tools" "latest"
```

The `latest` rename is required, not cosmetic: `sdkmanager` refuses to run from
a directory that is not named for a channel.

**3. Environment variables**, set for your user so they survive a new shell.
Adjust the JDK path if winget installed a different point release — the
`Get-ChildItem` below finds it.

```powershell
$jdk = (Get-ChildItem "$env:ProgramFiles\Eclipse Adoptium" -Directory |
        Where-Object Name -like "jdk-21*" | Select-Object -First 1).FullName

[Environment]::SetEnvironmentVariable("JAVA_HOME", $jdk, "User")
[Environment]::SetEnvironmentVariable("ANDROID_HOME", "$env:LOCALAPPDATA\Android\Sdk", "User")
```

Then **open a new terminal** — environment changes do not reach shells that are
already running.

**4. SDK packages.** `compileSdkVersion` is 36, set in
[`android/variables.gradle`](android/variables.gradle).

```powershell
$mgr = "$env:ANDROID_HOME\cmdline-tools\latest\bin\sdkmanager.bat"
& $mgr --licenses                     # accept all (type "y" repeatedly)
& $mgr "platform-tools" "platforms;android-36" "build-tools;36.0.0"
```

If sdkmanager reports that a package is unavailable, `& $mgr --list` shows what
the current channel actually offers.

**5. Verify.**

```powershell
java -version        # must report 17+
adb version
```

### Build and install

```bash
make app-apk        # debug APK, no IDE involved
make app-install    # the same, then adb install to a plugged-in phone
```

The APK lands at
`web-configurator/android/app/build/outputs/apk/debug/app-debug.apk`.

Plug the phone in with **USB debugging** enabled (Settings → Developer options).
`adb devices` should list it as `device`; `unauthorized` means the confirmation
prompt on the phone has not been accepted yet.

`make app-install` uses `adb install -r`, which replaces in place and keeps app
data, so saved presets survive a reinstall.

> ⚠️ **Test on a real phone. The Android emulator has no Bluetooth radio.** It
> emulates no BLE stack whatsoever, so the device chooser opens on an empty list
> and every connection attempt fails in a way that tells you nothing about the
> app. An emulator is useful here only for checking layout.

### Android Studio instead

If you would rather have Logcat, the layout inspector and the profilers:

```bash
make app-android    # syncs dist/ and opens the project
```

Studio manages its own JDK and SDK, so steps 1–4 above become unnecessary.

---

## iOS

**Requires macOS.** Xcode does not exist on other platforms. The Xcode *project*
generates fine anywhere — Capacitor 8 uses Swift Package Manager, so there is no
CocoaPods step to fail on Windows — but building or running it needs a Mac.

```bash
make app-ios        # syncs dist/ and opens Xcode
```

In Xcode, select the **App** target → **Signing & Capabilities** → set your
Team. Then pick a connected device (not a simulator) and press Run.

> ⚠️ **The iOS Simulator has no Bluetooth radio either**, for the same reason as
> the Android emulator. A real iPhone or iPad is the only way to test a
> connection.

Distribution needs the **Apple Developer Program** (US$99/yr) for TestFlight or
the App Store. Free sideloading through Xcode works for your own testing, but
the provisioning profile expires after **7 days** and the app then stops
launching until you rebuild it.

Once the app is approved, set `IOS_APP_STORE_URL` in
[`src/lib/platform.ts`](src/lib/platform.ts). The banner that iOS Safari users
see currently explains the situation without a link; filling this in turns it
into a button.

### Why iOS needs an app at all

WebKit ships no Web Bluetooth, no Web MIDI and no Web Serial. Every browser on
iOS is WebKit underneath — Chrome and Firefox there are skins over the same
engine — and an **installed PWA on iOS is still WebKit**. So no browser-based
route reaches a module from an iPhone, and a PWA would be an icon that opens a
page that cannot see the hardware. The native shell reaches the radio through
CoreBluetooth instead, running the identical page above it.

---

## Deploying the web build

Pushing to `main` with anything under `web-configurator/` changed publishes the
Controller to GitHub Pages, via
[`.github/workflows/deploy-controller.yml`](../.github/workflows/deploy-controller.yml).
It type-checks, builds, and uploads — roughly `make web-check && make web`.

**One-time setup, which CI cannot do for itself:** in the repository's
**Settings → Pages**, set **Source** to **GitHub Actions**. Until that is done
the deploy job fails with a 404 from the Pages API, which reads like a
permissions problem and is not one. Then run the workflow by hand from the
Actions tab — changing the Pages source does not trigger anything on its own.

The site is served from the custom domain **<https://alloy.vfmod.com/>**, which
is configured in Settings → Pages and is what makes `BASE_URL: /` correct below.

⚠️ **The base path is the whole reason this is not a four-line workflow, and it
depends on how the site is reached.** A custom domain serves from the root
(`BASE_URL: /`); a plain project site serves from `/<repo>/` and would need
`BASE_URL: /AlloyFlux/`. `vite.config.ts` feeds that value to Vite's `base`, the
manifest's `start_url` and `scope`, and the service worker's scope. Two failure
modes if it disagrees with reality, both silent:

- Assets resolve against the wrong prefix and 404 — a white page, nothing in the
  UI to say why.
- The manifest's `scope` disagrees with where the app actually lives, putting it
  out of scope by its own declaration. Chrome then refuses to install it and the
  **Install** button simply never appears, with no error logged anywhere.

`BASE_URL` defaults to `/`, and **nothing but the Pages job should ever set it to
anything else** — Capacitor serves the same `dist/` from the root of
`capacitor://localhost`, so a sub-path build produces an app whose every asset
404s.

⚠️ If the custom domain ever stops resolving, check Settings → Pages first: with
an Actions-based deploy the domain lives in that setting, not in a `CNAME` file
in the repository.

Icons are deliberately not regenerated in CI; see below for why.

## Icons

```bash
make web-icons
```

Regenerates every icon — PWA, iOS launcher, Android launcher, splash screens —
from `public/AlloyFlux_Logo.svg`. Outputs are committed; run this only when the
logo changes, then commit what it writes into `public/`, `ios/` and `android/`.

### ⚠️ Only three icons are round, and the rest are square on purpose

| Icon                           | Shape  | Why                                         |
| ------------------------------ | ------ | ------------------------------------------- |
| `pwa-{64,192,512}.png`         | round  | Nothing masks these — the shape is ours     |
| `maskable-icon-512x512.png`    | square | The launcher masks it (see below)           |
| `apple-touch-icon-180x180.png` | square | iOS masks it and drops alpha onto black     |
| `favicon.ico`                  | square | At 16px a disc wastes a fifth of the pixels |
| `ios/` + `android/` launchers  | square | Masked by the OS; iOS also forbids alpha    |

**Do not round the maskable icon.** `purpose: maskable` is a contract: we supply
a full-bleed square and the launcher crops it to whatever shape that device uses
— circle, squircle, rounded square, teardrop. Pre-rounding means a squircle
launcher exposes the transparent corners as bright wedges around the disc, and
the mask bites a second time into an already-inscribed circle. It looks right
only on launchers that happen to use a circle, and we do not pick those.

**Do not round or alpha the iOS app icon.** App Store Connect rejects an app
icon containing an alpha channel outright — it fails at upload, not at review.

The rounding lives in `ROUND_ICONS` in
[`scripts/gen-icons.mjs`](scripts/gen-icons.mjs). ⚠️ Everything in `public/` is
generated: hand-editing an icon there works until the next `make web-icons`
overwrites it.

⚠️ **The mark is pale mint line-art and needs the dark panel ground under it.**
Everything the pipeline produces is composited onto `#131518` — on white the
logo is very nearly invisible. Two places fight this and are handled in
[`scripts/gen-icons.mjs`](scripts/gen-icons.mjs): the PWA generator renders the
`purpose: any` icons and the favicon on transparency no matter what the config
says (only its maskable and apple paths honour a background), so they are
flattened afterwards; and `index.html` deliberately offers no SVG favicon,
because browsers prefer it over the `.ico` and the raw SVG has no ground.

If you swap the logo, also check the raster density: `gen-icons.mjs` probes for
it rather than hardcoding one, because an SVG sized in millimetres and one sized
in pixels rasterise to wildly different resolutions at the same DPI.

It is deliberately **not** part of `make web`: it needs `sharp`, a native module
that has to compile, and a fresh clone's first build should not fail on a
toolchain problem unrelated to the Controller.

---

## Analytics

The Google tag in [`index.html`](index.html) loads on the **hosted site only**.

⚠️ This matters because Capacitor wraps the same `dist/`: a plain `<script>` tag
there would ship analytics inside both store binaries, and Apple's App Privacy
labels and Play's Data Safety form would then both have to declare the
collection — undeclared collection is a rejection, not a warning.

The gate is on the **origin**, not on `window.Capacitor`. The bridge does set
that global, but whether it has run before an inline `<head>` script is an
injection-ordering detail that differs per platform, and guessing wrong fails in
the direction that ships the tag. The origins are fixed instead: iOS serves
`capacitor://localhost`, Android serves `https://localhost`. Local and LAN
addresses are excluded too, so neither `make web-dev` nor the `make web-host`
phone-testing loop puts sessions in the property.

## Things that will bite you

**`appId` is permanent.** `com.voltagefoundry.alloycontroller`, in
[`capacitor.config.ts`](capacitor.config.ts). Apple and Google both key an app's
identity to it, and neither lets you change it after the first submission
without shipping what users see as an entirely different app.

**`ios/` and `android/` are committed, and must not be regenerated.** They carry
the Info.plist Bluetooth usage string, the AndroidManifest permission set, the
signing configuration and the icon sets. `npx cap add` would silently discard
all four. The command you want is always `make app-sync`.

**A stale `dist/` means a stale app.** `cap sync` copies, it does not build —
which is why `app-sync`, `app-apk`, `app-install`, `app-ios` and `app-android`
all depend on `web`. Never run `npx cap sync` by hand expecting fresh code.

**`npm audit` reports issues in `@capacitor/cli`.** They are in its `xcode` →
`uuid` dependency chain, which is build-time tooling. Nothing from it ships in
either app.
