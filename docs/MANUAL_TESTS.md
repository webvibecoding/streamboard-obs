# Deferred Windows 11 smoke checklist

These checks have not been performed on the user's computer. Do not start or
control their OBS/game session without a new explicit request.

- Standard OBS 33.0.1: install while closed, confirm Plugin Manager lists the
  plugin without Legacy, open the dock, close/reopen OBS, uninstall.
- Portable OBS 33.0.1: extract ZIP, confirm dock and icon, remove plugin folder.
- Pair a fresh OBS device; restart OBS; disconnect; revoke from the web panel.
- On live Twitch and VK streams (including zero viewers), focus Steam/Epic games
  for 10–12 seconds and verify only categories change. Verify the VK title stays
  identical and offline platforms remain unchanged.
- Start an additional stream later and confirm category within 30–45 seconds.
- Alt+Tab to browser/OBS, close the game, switch games, pause/resume and sleep/wake.
- Unknown EXE: enter a name, select both platform categories independently.
- Edit saved mappings while the game is closed; fill a missing platform cell;
  reopen/restart and verify persistence and compatibility with Companion entries.
- Simulate network loss, ambiguous search and provider errors; confirm partial
  success and previous table cells remain visible.
- Open a second OBS instance; verify it cannot share the refresh-token session.
- Confirm daily/manual release check opens the plugin's release page only.
