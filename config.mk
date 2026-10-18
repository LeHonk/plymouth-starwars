# Per-demo configuration. This is the main file a fork edits
# (along with demo/ and assets/).

# Identifier: names the plugin .so, the theme directory and ModuleName.
# Lowercase letters, digits and dashes only.
DEMO_NAME   := star-wars
DEMO_TITLE  := Star Wars
DEMO_DESC   := A long time ago, in a boot far, far away

# Internal render resolution. The frame is integer-scaled and letterboxed
# onto every display, giving chunky pixels. Set both to 0 for native
# display resolution.
RENDER_W    := 320
RENDER_H    := 200
FPS         := 50

# QEMU test boot: how long the splash runs before the VM powers off
# (0 = forever, close the window to quit).
RUN_SECONDS := 40
