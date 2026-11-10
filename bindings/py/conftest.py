"""pytest 配置：库路径由命令行参数给出，不读环境变量。

全生态只允许 `KVSPACE` 一个环境变量——环境变量是隐式输入，不进命令行、不进日志，
两个人跑同一条命令可能结果不同。所以这里用 `--kvspace-lib=`，缺了就明确失败。

    python3 -m pytest bindings/py --kvspace-lib=/path/to/kvspace/build
"""

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
