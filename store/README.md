# The App Store release — what lives here and what is still a click

This folder is the App Store listing of **SHMUP Reborn** (App Store Connect app
`6783678258`, bundle `com.cabbry.shmup`), versioned like the rest of the game.
Two Linux-runner workflows read and write App Store Connect with the repo's
upload credentials; nothing here is a macOS minute.

## Layout

```
store/
  metadata/
    copyright.txt                  one line, the version's copyright
    en-US/  fr-FR/
      name.txt                     <= 30 characters (app-level)
      subtitle.txt                 <= 30 characters (app-level)
      privacy_url.txt              app-level, the privacy policy page
      description.txt              <= 4000
      keywords.txt                 <= 100, comma-separated
      promotional_text.txt         <= 170, can change without a new build
      whats_new.txt                <= 4000, the release notes
      support_url.txt  marketing_url.txt
  screenshots/
    en-US/  fr-FR/
      APP_IPHONE_67/ *.png         6.9"/6.7" iPhone: 1320x2868 or 1290x2796 (required)
      APP_IPAD_PRO_3GEN_129/ *.png 13"/12.9" iPad: 2064x2752 or 2048x2732 (required, the app runs on iPad)
  PRIVACY.md                       the privacy policy the listing links to
  review_notes.txt                 the notes for App Review
```

Screenshots are sorted by file name inside a folder; the first one is the one
the store shows first. Up to ten per set. Take them **on a device** (the
Simulator's timing is not the game's): Settings > Control Centre has a
screenshot button, and the frames land in Photos at the device's native size.
An iPhone 15/16 Pro Max frame is 1290x2796 or 1320x2868, both accepted for
the 6.9" set; an iPad Pro 12.9"/13" frame is 2048x2732 or 2064x2752.

## The workflows

- **`asc-store-state.yml`** (read-only) prints the listing as App Store Connect
  has it: the app-level info (categories, age rating, localizations), every
  version with its state and attached build, the localizations' field
  lengths, the screenshot sets, whether review details and pricing exist.
  Run it before and after a push.
- **`asc-store-push.yml`** writes this folder to App Store Connect for one
  version string (input `version`, e.g. `5.0.10`): creates the version if it
  does not exist, sets the copyright, the localizations, the app-level
  name/subtitle/privacy URL, the primary category (Games: Action, Arcade),
  the review notes, attaches the newest VALID build of that version, and
  replaces the screenshot sets from `store/screenshots`. It **never submits
  for review**: submission is a human click, after Fabien's word.
  `dry_run: true` prints what it would change and writes nothing.

## Still a click in App Store Connect (no public API, or personal data)

1. **App privacy** (App Privacy section): answer **"Data Not Collected"**.
   The app has no server; Game Center's collection is Apple's to disclose
   ("You are not responsible for disclosing data collected by Apple");
   settings and loadout stay on the device. `PrivacyInfo.xcprivacy` in the
   bundle says the same (no tracking, no collected data, NSUserDefaults for
   reason CA92.1).
2. **Age rating** questionnaire: *Cartoon or Fantasy Violence: Infrequent or
   Mild*; everything else *None*; no gambling, no contests, no unrestricted
   web access, no user-generated content, no messaging. Expected rating: 9+.
3. **Pricing and availability**: Free, all territories. (A GPLv3 game has to
   be free to download; the source is public anyway.)
4. **App Review contact** (name, phone, e-mail) in the version's review
   details: personal data, so it is not in this repository. Sign-in
   information: *not required*.
5. **Content rights**: the app does not contain third-party content. The two
   music tracks shipped with the 2009 game are part of Fabien's release.
6. **Submit for review** once Fabien has answered and the build is the one he
   saw. Release: *manual* after approval, so the day is chosen.

## Sizes to respect (App Store Connect refuses beyond)

| field | limit |
|---|---|
| name, subtitle | 30 |
| promotional text | 170 |
| keywords | 100 |
| description, what's new | 4000 |
| review notes | 4000 |
