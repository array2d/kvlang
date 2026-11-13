
import pytest

import pathspace

def pytest_addoption(parser):
    parser.addoption(
        "--pathspace-lib",
        action="store",
        default=None,
        help="Directory containing libpathspace (required)",
    )

def pytest_configure(config):
    lib_directory = config.getoption("--pathspace-lib")
    if not lib_directory:
        raise pytest.UsageError(
            "Missing --pathspace-lib=<dir containing libpathspace>. Example:\n"
            "  python3 -m pytest bindings/py --pathspace-lib=/path/to/pathspace/build"
        )
    pathspace.configure(lib_directory)
