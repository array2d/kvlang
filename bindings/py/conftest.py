
import pytest

import kvspace

def pytest_addoption(parser):
    parser.addoption(
        "--kvspace-lib",
        action="store",
        default=None,
        help="内含 libkvspace 的目录（必填）",
    )

def pytest_configure(config):
    lib_directory = config.getoption("--kvspace-lib")
    if not lib_directory:
        raise pytest.UsageError(
            "缺少 --kvspace-lib=<内含 libkvspace 的目录>。例：\n"
            "  python3 -m pytest bindings/py --kvspace-lib=/path/to/kvspace/build"
        )
    kvspace.configure(lib_directory)
