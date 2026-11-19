# Security

Report suspected vulnerabilities privately to the repository owner using
[GitHub private vulnerability reporting](https://github.com/webvibecoding/streamboard-obs/security/advisories/new).
Do not post device codes, credentials or private OBS logs in public issues.

The production API origin is fixed to https://streamboard.sip1ay.pro. HTTP
redirects and cookie authentication are disabled for the desktop client. Pairing
uses PKCE and only opens HTTPS URLs on that exact origin. Device access is
scoped by the server; provider OAuth credentials are never delivered to the plugin.

Requests have a timeout and bounded response size. Refresh rotation is serialized.
Queued syncs revalidate the currently observed game before sending. Installing or
updating the plugin requires OBS to be closed. Release binaries are unsigned;
SHA256SUMS.txt covers every distributed binary/package and the SBOM.

Because native plugins run in OBS's process, a defect can affect OBS. Use the
provided CI evidence and deferred manual checklist when evaluating a release.
