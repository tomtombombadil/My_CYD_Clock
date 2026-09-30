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
#
# It also leaves a copy of the joined file in a folder named for the version,
# for example release-v1.13.4, under a name that says which board it is for,
# with a list of checksums alongside. Those are the files to attach to a
# GitHub release. The folder is left out of Git on purpose, because the files
# are the same ones already in docs/firmware.
#
# Nothing here affects the firmware itself.

Import("env")

import hashlib
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

# What each board's file is called in a release. Someone who has just landed
# on the releases page needs the file name alone to tell them which one to take.
RELEASE_NAMES = {
    "cyd28": "2.8in-ESP32-2432S028R",
    "cyd32": "3.2in-ESP32-2432S032R",
    "cyd35": "3.5in-4.0in-ESP32-3248S035R",
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
        "version": "v" + version,
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

    release_file = copy_for_release(project_dir, board, version, merged)

    size = os.path.getsize(merged)
    print("")
    print("Ready to flash: docs/firmware/%s.bin  (%.0f KB, v%s)"
          % (board, size / 1024.0, version))
    print("Write it to the board starting at address 0.")
    if release_file:
        print("Copy for a GitHub release: %s" % release_file)
    print("")


def copy_for_release(project_dir, board, version, merged):
    """Puts a board-named copy in release-v<version> and rewrites the checksum
    list to cover every file in that folder. Each board is built on its own,
    so the list is rebuilt from whatever is there rather than appended to."""
    label = RELEASE_NAMES.get(board)
    if not label or version == "unknown":
        return None
    folder_name = "release-v%s" % version
    folder = os.path.join(project_dir, folder_name)
    os.makedirs(folder, exist_ok=True)
    name = "My_CYD_Clock-v%s-%s.bin" % (version, label)
    shutil.copyfile(merged, os.path.join(folder, name))

    lines = []
    for entry in sorted(os.listdir(folder)):
        if not entry.endswith(".bin"):
            continue
        digest = hashlib.sha256()
        with open(os.path.join(folder, entry), "rb") as f:
            for block in iter(lambda: f.read(65536), b""):
                digest.update(block)
        lines.append("%s  %s\n" % (digest.hexdigest(), entry))
    with open(os.path.join(folder, "SHA256SUMS.txt"), "w", newline="\n") as f:
        f.writelines(lines)
    return "%s/%s" % (folder_name, name)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
