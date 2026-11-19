# Privacy

The plugin operates while OBS is open. Pause stops automatic sync. Closing OBS
stops the plugin. It does not register a Windows startup task or a background service.

Locally it reads the foreground HWND/PID, executable basename and image path,
ProductName/FileDescription version resources, Steam library/ACF files and Epic
manifest files. Paths are used only for local matching and SHA256 game identities.
Window titles are not read. There is no process-list upload or gameplay capture.

Streamboard receives game key, source (steam/epic/exe), display name, executable
basename, observation time and event ID. EXE keys are SHA256 identifiers; hashing
is not a guarantee against guessing a known path. Manually entered game names are
sent when saving mappings and synchronizing. Never enter private information there.

Tokens for Twitch and VK stay on Streamboard. Device credentials are held in
Windows Credential Manager under Streamboard.OBS.DeviceSession.v1, separately
from Companion. A user-scoped named mutex prevents simultaneous plugin sessions
from rotating the same refresh token. Credentials are not written into OBS profiles
or plugin settings. Revoking the device in Streamboard removes its server access.

OBS logs contain plugin version, loading/shutdown outcomes and fixed diagnostic
codes only. Raw HTTP bodies, tokens, full paths and window titles are not logged.
OBS itself and other plugins maintain their own logs, outside this plugin's control.

Local configuration under OBS's plugin configuration directory stores pause state,
last update check time, and EXE hash-to-name rules. It contains no full game paths.
Saved platform mappings are stored on Streamboard and shared across devices of
the same account. Disconnect the device before uninstalling if access should be
revoked; uninstalling intentionally retains credentials and settings for reinstalls.

Update checks contact GitHub's public latest-release API at most once per day,
or when requested. GitHub receives the normal IP address and User-Agent. There is
no telemetry or automatic binary download/install.
