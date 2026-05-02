# asm-differ project config for asm-verify.
#
# asm-differ looks for diff_settings.py in the cwd at startup. This file
# tells it the target arch and which objdump binary to use for the port-side
# disassembly.

def apply(config, args):
    config["arch"] = "arm32"
    # Sourcery 2010q1 binutils lives at /opt/sourcery-2010q1/bin/.
    config["objdump_executable"] = "/opt/sourcery-2010q1/bin/arm-none-eabi-objdump"
    config["map_format"] = "gnu"
    config["show_line_numbers_default"] = False
