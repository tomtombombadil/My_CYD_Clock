# merge_firmware.py
#
# Runs automatically after every build. It does two things.
#
# First, it joins the four pieces a blank ESP32 needs into one file that can be
# written starting at address zero. A normal build produces those four pieces
# separately, at four different addresses, which is fine when PlatformIO is
# doing the writing because it knows where each one goes. Anyone flashing the
# board any other way would have to know all four addresses. One merged file
# removes that entirely.
#
# Second, it writes the small description file the web flashing page reads, so
# the version shown there can never drift out of step with the firmware.
#
# Both land in the docs folder, which is what GitHub publishes as a web page.
# Nothing here affects the firmware itself.

Import("env")

import json
import os
import re
import shutil

PARTS_ADDRESSES = {
    "bootloader":  0x1000,
    "partitions":  0x8000,
    "boot_app0":   0xE000,
    "application": 0x10000,
}

BOARD_TITLES = {
    "cyd28": "2.8 inch  (ESP32-2432S028R)",
    "cyd32": "3.2 inch  (ESP32-2432S032R)",
    "cyd35": "3.5 and 4.0 inch  (ESP32-3248S035R)",
}


def firmware_version(project_dir):
    """Reads the version out of src/config.h so there is only one place it
    is ever written down."""
    header = os.path.join(project_dir, "src", "config.h")
    try:
        with open(header) as f:
            found = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
            if found:
                return found.group(1)
    except OSError:
        pass
    return "unknown"


def merge(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")
    board       = env.subst("$PIOENV")
    version     = firmware_version(project_dir)

    out_dir = os.path.join(project_dir, "docs", "firmware")
    os.makedirs(out_dir, exist_ok=True)

    framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    pieces = {
        "bootloader":  os.path.join(build_dir, "bootloader.bin"),
        "partitions":  os.path.join(build_dir, "partitions.bin"),
        "boot_app0":   os.path.join(framework, "tools", "partitions", "boot_app0.bin"),
        "application": os.path.join(build_dir, env.subst("$PROGNAME") + ".bin"),
    }

    for name, path in pieces.items():
        if not os.path.isfile(path):
            print("merge_firmware: cannot find the %s piece at %s" % (name, path))
            return

    merged = os.path.join(out_dir, "%s.bin" % board)

    command = [
        '"$PYTHONEXE"', '"$OBJCOPY"',
        "--chip", "esp32", "merge_bin",
        "-o", '"%s"' % merged,
        "--flash_mode", env.BoardConfig().get("build.flash_mode", "dio"),
        "--flash_freq", "40m",
        "--flash_size", env.BoardConfig().get("upload.flash_size", "4MB"),
    ]
    for name in ("bootloader", "partitions", "boot_app0", "application"):
        command += [hex(PARTS_ADDRESSES[name]), '"%s"' % pieces[name]]

    if env.Execute(" ".join(command)) != 0:
        print("merge_firmware: joining the pieces failed")
        return

    # The description file the web flashing page reads. Every one of these
    # boards is a plain ESP32, so each board gets its own file and the page
    # offers one button per board rather than guessing.
    manifest = {
        "name": "My CYD Clock  -  %s" % BOARD_TITLES.get(board, board),
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32",
                "parts": [{"path": "firmware/%s.bin" % board, "offset": 0}],
            }
        ],
    }
    manifest_path = os.path.join(project_dir, "docs", "manifest-%s.json" % board)
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    size = os.path.getsize(merged)
    print("")
    print("Ready to flash: docs/firmware/%s.bin  (%.0f KB, version %s)"
          % (board, size / 1024.0, version))
    print("Write it to the board starting at address 0.")
    print("")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
