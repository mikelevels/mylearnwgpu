"""List YOUR source files, leaving out third-party code and build folders.

Used by the CI checks (.github/workflows/ci.yml) so that lizard, Cppcheck and
clang-tidy judge only your code, the same way the 2026-10-15 sweep did.

Usage:
    python .github/scripts/own_files.py            every .cpp/.h/.hpp, one per line
    python .github/scripts/own_files.py --cpp      only the .cpp files
    python .github/scripts/own_files.py --headers  only the .h/.hpp files

WHY A LIST OF THIRD-PARTY PATHS (and not a list of your files):
a new file you add (for example engine/Renderer.cpp) is checked automatically.
When you copy in a new library, add its folder or file name to THIRD_PARTY.
"""
import sys
from pathlib import Path

# Folders and files that are NOT yours. A name matches at any depth.
THIRD_PARTY = {
    # folders
    "glfw", "glfw3webgpu", "glm", "webgpu", "imgui", "tinyobjloader",
    ".git", ".github", ".vs", "out",
    # single files copied from other projects
    "stb_image.h", "tiny_obj_loader.h",
}
# Files that only compile someone else's code (webgpu.hpp's function bodies)
IMPLEMENTATION_SUFFIX = "-implementation.cpp"

CPP = {".cpp"}
HEADERS = {".h", ".hpp"}


def is_third_party(path: Path) -> bool:
    if path.name.endswith(IMPLEMENTATION_SUFFIX):
        return True
    for part in path.parts:
        # build-wgpu, build-dawn, build-emscripten, ... are build folders
        if part in THIRD_PARTY or part.startswith("build"):
            return True
    return False


def main() -> None:
    wanted = CPP | HEADERS
    if "--cpp" in sys.argv:
        wanted = CPP
    elif "--headers" in sys.argv:
        wanted = HEADERS

    root = Path(".")
    files = sorted(
        p.as_posix()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in wanted and not is_third_party(p)
    )
    # "\n" line ends even on Windows, where bash would otherwise read the
    # "\r" of "\r\n" as part of each file name
    sys.stdout.reconfigure(newline="\n")
    print("\n".join(files))


if __name__ == "__main__":
    main()
