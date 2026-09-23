# HHKBS

Unofficial HHKB Studio keymap editor for the US layout on Linux.

![HHKBS editing an HHKB Studio US profile](docs/images/hhkbs-main.png)

## Download

Release builds will be available on [GitHub Releases](https://github.com/07LEE/hhkbs/releases).

## Usage

1. Connect the HHKB Studio over USB and open HHKBS.
2. Select Base, Fn1, Fn2, or Fn3.
3. Select a key, mouse button, or gesture-pad direction and assign a function.
4. Export the edited profile as TOML.

## Device permissions

If HHKBS reports a permission error, download [60-hhkbs.rules](packaging/60-hhkbs.rules). Run these commands from the folder containing the downloaded file, then reconnect the keyboard:

```bash
sudo install -m 0644 60-hhkbs.rules /etc/udev/rules.d/60-hhkbs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## License

[MIT](LICENSE). [Third-party licenses](third_party/README.md).
