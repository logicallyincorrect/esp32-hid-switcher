"""Use the pinned SDK's URB definition, not a locally guessed ABI."""
from pathlib import Path
import sys

Import("env")
sdk = Path(env.PioPlatform().get_package_dir("framework-espidf"))
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from usb_stack_patch import patch_hub_source

# Compile a corrected copy as an application object. The original SDK package
# and its archive remain untouched; the linker uses our hub object instead.
out = Path(env.subst("$PROJECT_DIR")) / ".pio/usb-stack-fixes"
out.mkdir(parents=True, exist_ok=True)
target = out / "patched_usb_hub.c"
patched = patch_hub_source((sdk / "components/usb/hub.c").read_text())
if not target.exists() or target.read_text() != patched:
    target.write_text(patched)
env.Append(CPPPATH=[str(sdk / "components/usb/private_include"), str(out)])
