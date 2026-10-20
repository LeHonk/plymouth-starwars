# Star Wars boot splash

*A long time ago, in a boot far, far away…*

A Star Wars opening sequence as a native Plymouth boot splash, done in chunky 320×200 pixels at 50 fps:

1. The boot mode (**BOOT**, **SHUTDOWN**, **REBOOT** or **UPDATE**) appears as a huge hollow yellow logo and recedes into the centre of the screen.
2. After 3.5 s the opening crawl starts. An episode title comes first, then every boot message as it arrives, tilted back and scrolling away towards the vanishing point.
3. The whole time, stars stream out from the centre past a sliver of a blue planet in the lower right corner.

Press any key to jump to hyperspace.

Each kind of boot gets its own episode:

| Mode | Episode |
|---|---|
| boot | Episode IV: *A NEW BOOT* |
| reboot | Episode VI: *RETURN OF THE BOOT* |
| shutdown | Episode VIII: *THE LAST SHUTDOWN* |
| updates | Episode I: *THE PHANTOM UPDATE* |

Password prompts for encrypted disks are drawn on top of the crawl, so LUKS boots keep working.

## Try it

```sh
make preview     # SDL window, no Plymouth needed (needs libsdl2-dev)
make run         # real plymouthd in a QEMU window, no root, no disk image
make run APPEND=demo.password   # same, with a password prompt after 3 s
```

`make run` boots your host kernel with a 4 MB initramfs, plays fake boot messages for 40 s (`RUN_SECONDS` in `config.mk`) and powers off. Press Ctrl-A X in the terminal to quit early.

## Install

```sh
make && sudo make install
sudo plymouth-set-default-theme -R star-wars
```

`make` fetches the Plymouth headers by itself if `libplymouth-dev` isn't installed. Plymouth has no stable plugin ABI, so build on the machine (or distro release) you install on.

## Tweaking

- `config.mk`: title, render size, frame rate and test run length.
- `demo/demo.c`: the effects. Timings, colours, star count and crawl speed are `#define`s at the top of the file.

## Built on plymouth-demo

This repo is a fork of [plymouth-demo](https://github.com/LeHonk/plymouth-demo), a base for old-school demoscene boot splashes. plymouth-demo provides the Plymouth plugin glue, scaling, prompt overlay, the SDL previewer and the QEMU/VM/PXE test tooling. This fork adds only `config.mk` and `demo/`.

See the upstream README for the demo API, the full set of test targets (`make run-vm`, `make pxe`), troubleshooting, and how to start your own demo. To pull in fixes from the base:

```sh
git pull upstream main
```
