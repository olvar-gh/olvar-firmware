# PlatformIO pre-script: defines OLVAR_FW_VERSION as "<custom_fw_version>+<git short hash>",
# e.g. "0.1.0+a1b2c3d". Falls back to the bare version when git is unavailable.
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

version = env.GetProjectOption("custom_fw_version")  # noqa: F821
try:
    short_hash = (
        subprocess.check_output(
            ["git", "rev-parse", "--short=7", "HEAD"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
        )
        .decode()
        .strip()
    )
    fw = f"{version}+{short_hash}" if short_hash else version
except (OSError, subprocess.CalledProcessError):
    fw = version

env.Append(CPPDEFINES=[("OLVAR_FW_VERSION", env.StringifyMacro(fw))])  # noqa: F821
