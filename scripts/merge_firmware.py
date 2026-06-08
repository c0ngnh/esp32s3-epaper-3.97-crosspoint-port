"""PlatformIO post-build script: merge bootloader, partitions, and app at 0x0."""

Import("env")
import os


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    app_bin = os.path.join(build_dir, f"{progname}.bin")
    merged_path = os.path.join(build_dir, f"{progname}.merged.bin")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")

    for path in (bootloader, partitions, app_bin):
        if not os.path.isfile(path):
            print(f"WARNING [merge_firmware.py]: missing {path}, skipping merged image")
            return

    board = env.BoardConfig()
    chip = board.get("build.mcu", "esp32s3")
    flash_size = board.get("upload.flash_size", "16MB")
    f_flash = str(board.get("build.f_flash", "80000000L"))
    flash_freq = "80m" if f_flash.startswith("80") else "40m"
    flash_mode = board.get("build.flash_mode", "qio")

    esptool_pkg = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool_py = os.path.join(esptool_pkg, "esptool.py")
    python = env.subst("$PYTHONEXE")

    cmd = (
        f'"{python}" "{esptool_py}" --chip {chip} merge_bin '
        f'-o "{merged_path}" '
        f'--flash_mode {flash_mode} --flash_freq {flash_freq} --flash_size {flash_size} '
        f'0x0 "{bootloader}" 0x8000 "{partitions}" 0x10000 "{app_bin}"'
    )
    print(f"CrossPoint merged flash image: {merged_path}")
    if env.Execute(cmd) != 0:
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
