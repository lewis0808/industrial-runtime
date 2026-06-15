cmake_minimum_required(VERSION 3.20)
project(py_s7_plugin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 直接复用 tools/S7ServerMock 的 .venv（已装 python-snap7 2.0.2），不走 vcpkg。
set(VENV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../S7ServerMock/.venv")
if(NOT EXISTS "${VENV_DIR}/pyvenv.cfg")
    message(FATAL_ERROR "未找到 venv: ${VENV_DIR} —— 先在 tools/S7ServerMock 建好 .venv")
endif()

# venv 本身不含开发头/库（Python.h / python39.lib）。从 pyvenv.cfg 的 home 取创建该 venv 的
# 基础 Python 安装目录（其下有 include/ 与 libs/），用它来 find_package + 链接。
file(READ "${VENV_DIR}/pyvenv.cfg" _PYVENV_CFG)
string(REGEX MATCH "home *= *([^\r\n]+)" _IGNORED "${_PYVENV_CFG}")
file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" PY_BASE)
message(STATUS "PyS7 bridge: 基础 Python=${PY_BASE}  venv=${VENV_DIR}")

# 用基础安装的开发文件（venv 无 include/lib）。ROOT_DIR 优先；FIND_VIRTUALENV STANDARD 表示
# 不让"已激活的 venv"抢占（合法值仅 FIRST/ONLY/STANDARD）；FIND_REGISTRY 也不走注册表。
set(Python3_ROOT_DIR "${PY_BASE}")
set(Python3_FIND_REGISTRY NEVER)
set(Python3_FIND_VIRTUALENV STANDARD)
find_package(Python3 3.9 EXACT REQUIRED COMPONENTS Development)

# 插件本体：MODULE（动态加载库）。PREFIX "" -> py_s7_plugin.dll / .so。
add_library(py_s7_plugin MODULE pyhost_bridge.cpp)
set_target_properties(py_s7_plugin PROPERTIES PREFIX "")
# 全局 vcpkg 的 MSBuild 集成会给本工程注入 applocal 后置步骤（新版用 pwsh，未装即报错）。
# 本示例不依赖 vcpkg，直接在 vcxproj 里关掉它。
set_target_properties(py_s7_plugin PROPERTIES VS_GLOBAL_VcpkgEnabled false)
# 中文 locale 下 MSVC 默认按 GBK(936) 读源码，会把 UTF-8 中文注释解析错乱（C4819 + 连锁语法错误）。
if(MSVC)
    target_compile_options(py_s7_plugin PRIVATE /utf-8)
endif()
target_include_directories(py_s7_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk/plugin-sdk/example) # irplugin/plugin_abi.h
target_link_libraries(py_s7_plugin PRIVATE Python3::Python)

# 把两个路径烤进二进制，桥在运行期用：
#  - PYHOST_PYTHON_HOME：基础 Python（嵌入解释器据此找标准库，不依赖 python39.dll 的位置）。
#  - PYHOST_VENV_SITE  ：venv 的 site-packages（snap7 仅在此），桥插入 sys.path 使 import 生效。
target_compile_definitions(py_s7_plugin PRIVATE
    PYHOST_PYTHON_HOME="${PY_BASE}"
    PYHOST_VENV_SITE="${VENV_DIR}/Lib/site-packages")
