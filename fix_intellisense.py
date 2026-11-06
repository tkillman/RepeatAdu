Import("env")
import os

pkg_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-esp32c3-libs")
if pkg_dir:
    inc_prefix = os.path.join(pkg_dir, "include")
    flags_file = os.path.join(pkg_dir, "flags", "includes")
    if os.path.isfile(flags_file):
        with open(flags_file, "r") as f:
            content = f.read()
        tokens = content.split()
        paths = []
        for i in range(len(tokens) - 1):
            if tokens[i] == "-iwithprefixbefore":
                rel_path = tokens[i + 1]
                full_path = os.path.abspath(os.path.join(inc_prefix, rel_path)).replace("\\", "/")
                if os.path.isdir(full_path):
                    paths.append(full_path)
        env.Append(CPPPATH=paths)
