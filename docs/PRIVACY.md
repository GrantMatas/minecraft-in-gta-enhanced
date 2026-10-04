# Public source export

The export uses an explicit allowlist: source, tests, build bootstrap, clean templates, licenses and general documentation.

Excluded: saves/worlds/player data; profiles/accounts; logs/diagnostics/hardware reports; screenshots/recordings/attachments; generated install manifests/ReShade settings; private paths/session notes; downloaded runtimes/SDKs/mod JARs; caches/builds/local EXEs; credentials/tokens and personal Git author/email settings.

Standard Gradle bootstrap files and upstream public copyright attribution are retained. Generated configuration stays in ignored `launcher.local.json`, `diagnostics/`, `logs/`, `runtime/`, `.cache/` and `mc/run/`.

`scripts/export-public.ps1` builds a clean export and checks private path/credential patterns. Review its file list and contents before publication; do not blindly publish the live workspace or its history. Public GitHub ownership is inherently visible; use a GitHub noreply address rather than a private email for commits.
