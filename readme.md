# HHKBS

Unofficial HHKB Studio keymap editor for the US layout on Linux.

![HHKBS editing an HHKB Studio US profile](docs/images/hhkbs-main.png)

## Download

Release builds will be available on [GitHub Releases](https://github.com/07LEE/hhkbs/releases).

## Device permissions

HHKBS needs read and write access to the keyboard's hidraw device, which Linux usually restricts to root. If HHKBS reports a permission error, download [60-hhkbs.rules](packaging/60-hhkbs.rules). Run these commands from the folder containing the downloaded file, then reconnect the keyboard:

```bash
sudo install -m 0644 60-hhkbs.rules /etc/udev/rules.d/60-hhkbs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Usage

1. Connect the HHKB Studio over USB and open HHKBS. The profile the keyboard is using is loaded.
2. Choose Profile 1 to 4 to edit another profile.
3. Select Base, Fn1, Fn2, or Fn3.
4. Select a key, mouse button, or gesture-pad direction and assign a function.
5. Choose Apply to keyboard to write the profile, or Export to save it as TOML.

Read from keyboard reloads the profile the keyboard is currently using, for example after you switch profiles on the keyboard itself.

## Applying to the keyboard

Apply to keyboard overwrites the selected profile on the keyboard. HHKBS first saves the profile the keyboard currently holds as a TOML backup in `~/.local/state/hhkbs/backups/` (or under `$XDG_STATE_HOME/hhkbs/backups/`), writes the new profile, and reads it back to check it. If the check fails, HHKBS tries to restore the backup. The keyboard returns to the profile it was using before, and Restore from backup lists the saved backups so one can be loaded into the editor or restored to the keyboard right away. Backups you no longer need can be deleted there; HHKBS never deletes them on its own.

Do not unplug the keyboard while it is being written. HHKBS is unofficial, so keep your own backup of profiles you care about.

Restore defaults puts the built-in default keymap in the editor. It does not change the keyboard until you apply it.

## License

[MIT](LICENSE). [Third-party licenses](third_party/README.md).
