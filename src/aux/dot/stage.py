"""Stage Graphviz inputs and generate parsers/tables outside its checkout."""
from pathlib import Path
import re
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[3]
upstream = repo / "submodules/graphviz"
build = Path(sys.argv[1]).resolve()
if not build.is_relative_to(repo / "build"):
    raise SystemExit("dot outputs must stay under the repository build directory")
source = build / "source"
if source.exists():
    shutil.rmtree(source)
source.mkdir(parents=True)
for name in ["lib", "cmd/dot", "plugin/core", "plugin/dot_layout"]:
    shutil.copytree(upstream / name, source / name, ignore=shutil.ignore_patterns(".git"))
subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(repo / "src/aux/dot/waste.patch")], cwd=source, check=True)
for name in ["COPYING", "LICENSE", "EPL-2.0.txt", "CHANGELOG.md", "graphviz_version.h.in"]:
    shutil.copy2(upstream / name, source / name)
marker = upstream / ".waste-source-revision"
revision = marker.read_text().strip() if marker.exists() else subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
version = re.search(r"^## \[(?:Unreleased \()?([0-9]+\.[0-9]+\.[0-9]+)", (source / "CHANGELOG.md").read_text(), re.M)[1]
components = version.split(".")
version += "-waste.g" + revision[:12]
header = (source / "graphviz_version.h.in").read_text()
values = {"GVPLUGIN_CURRENT": "8", "GRAPHVIZ_VERSION": version, **dict(zip(["GRAPHVIZ_VERSION_MAJOR", "GRAPHVIZ_VERSION_MINOR", "GRAPHVIZ_VERSION_PATCH"], components))}
for name, value in values.items():
    header = header.replace("@" + name + "@", value)
(source / "graphviz_version.h").write_text(header)
(source / "builddate.h").write_text('#define BUILDDATE "' + revision[:12] + '"\n')
(source / ".source-revision").write_text(revision + "\n")
subprocess.run(["bison", "-d", "--output=grammar.c", "grammar.y"], cwd=source / "lib/cgraph", check=True)
subprocess.run(["flex", "--case-insensitive", "--outfile=scan.c", "scan.l"], cwd=source / "lib/cgraph", check=True)
subprocess.run(["bison", "-d", "--output=htmlparse.c", "htmlparse.y"], cwd=source / "lib/common", check=True)
common = source / "lib/common"
subprocess.run([sys.executable, "make_colortbl.py", "brewer_colors", "svgcolor_names", "color_names", "colortbl.h"], cwd=common, check=True)
subprocess.run([sys.executable, "entities.py", "entities.h"], cwd=common, check=True)

