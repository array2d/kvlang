
import pytest

import pathspace

def pytest_addoption(parser):
    parser.addoption(
        "--pathspace-lib",
        action="store",
        default=None,
        help="内含 libpathspace 的目录（必填）",
    )

def pytest_configure(config):
    lib_directory = config.getoption("--pathspace-lib")
    if not lib_directory:
        raise pytest.UsageError(
            "缺少 --pathspace-lib=<内含 libpathspace 的目录>。例：\n"
            "  python3 -m pytest bindings/py --pathspace-lib=/path/to/pathspace/build"
        )
    pathspace.configure(lib_directory)
