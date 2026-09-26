# HHKBS

Unofficial HHKB Studio keymap editor for the US layout on Linux.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/hhkbs-main-dark.png">
  <img alt="HHKBS editing an HHKB Studio US profile" src="docs/images/hhkbs-main-light.png">
</picture>

## Download

Release builds will be available on [GitHub Releases](https://github.com/07LEE/hhkbs/releases).

## Device permissions

If HHKBS reports a permission error, download [60-hhkbs.rules](packaging/60-hhkbs.rules). Run these commands from the folder containing the downloaded file, then reconnect the keyboard:

```bash
sudo install -m 0644 60-hhkbs.rules /etc/udev/rules.d/60-hhkbs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Usage

1. Connect the HHKB Studio over USB and open HHKBS. Over Bluetooth it can read the profile and switch the gesture pads; applying needs USB.
2. Choose a profile (1 to 4) and a layer, select a key, and assign a function. Each profile keeps its own edits.
3. Choose Apply to keyboard and pick the profiles to write. Save keeps the profile as a backup.

Apply saves what the keyboard held to `~/.local/state/hhkbs/backups/` before writing. Do not unplug the keyboard while it is being written.

## Disclaimer

HHKBS is an unofficial project. It is not affiliated with or endorsed by PFU Limited, and HHKB and Happy Hacking Keyboard are trademarks of PFU Limited.

HHKBS writes to your keyboard. Use it at your own risk: the authors are not responsible for lost settings, a misbehaving keyboard, or any other damage caused by using it.

## License

[MIT](LICENSE). [Third-party licenses](third_party/README.md).
