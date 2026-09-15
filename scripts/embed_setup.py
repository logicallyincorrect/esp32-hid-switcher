from pathlib import Path
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
page = (root / "web/index.html").read_text()
assert ')SETUP"' not in page
header = '#pragma once\nstatic const char SETUP_PAGE[] PROGMEM = R"SETUP(' + page + ')SETUP";\n'
target = root / 'src/SetupPage.h'
if not target.exists() or target.read_text() != header:
    target.write_text(header)
