// PyHost 桥：让 Python 写运行时插件。
//
// 宿主只能加载原生 DLL（LoadLibrary/dlopen），解释型语言无法直接产出这种 DLL，
// 故需要这层桥：它导出 C-ABI v3 的 getPluginInfo/createPlugin，内部嵌 CPython，把宿主
// IrPluginHostApi* 的地址原样交给 Python，由 s7_driver.py 用 ctypes 直接调 push_tag。
//
// 桥本身不懂 S7、不碰 TagValue —— 设备逻辑全在 Python。桥是通用的、写一次即可复用。
// 生命周期蹦床全是纯 C 函数指针（IrPluginInstance），不跨 DLL 传 C++ 对象/vtable。
//
// 约束：嵌入的 CPython 须能 import s7_driver 及其依赖（python-snap7）。本桥把自身 DLL
// 所在目录加入 sys.path（s7_driver.py 与 dll 同目录部署）；snap7 等第三方包需安装在所
// 链接/PYTHONHOME 指向的那个 Python 环境里。

#include <Python.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "irplugin/plugin_abi.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr const char *kModule = "s7_driver"; // 固定模块名（示例）；通用桥可改为从配置读取

// 每个插件实例的状态。self 指向它，宿主在 destroy 时释放。
struct PyState {
    const IrPluginHostApi *host;
    std::string configPath;
    PyObject *module; // s7_driver 模块对象（新引用）
};

// 调用 Python 模块的无参 bool 函数（init 除外），全程持 GIL。
int callBool(PyState *s, const char *fn) {
    if (s->module == nullptr) {
        return 0;
    }
    const PyGILState_STATE g = PyGILState_Ensure();
    int rc = 0;
    PyObject *f = PyObject_GetAttrString(s->module, fn);
    if (f != nullptr && PyCallable_Check(f) != 0) {
        PyObject *r = PyObject_CallNoArgs(f);
        if (r != nullptr) {
            rc = PyObject_IsTrue(r) != 0 ? 1 : 0;
            Py_DECREF(r);
        } else {
            PyErr_Print(); // Python 异常不跨 C-ABI 逃逸，打印后按失败处理
        }
    }
    Py_XDECREF(f);
    PyGILState_Release(g);
    return rc;
}

// 生命周期蹦床（C 函数指针，宿主以 self 为首参调用）。
int pyInit(void *self) {
    auto *s = static_cast<PyState *>(self);
    const PyGILState_STATE g = PyGILState_Ensure();
    int rc = 0;
    PyObject *mod = PyImport_ImportModule(kModule);
    if (mod != nullptr) {
        s->module = mod; // 持有
        PyObject *f = PyObject_GetAttrString(mod, "init");
        if (f != nullptr && PyCallable_Check(f) != 0) {
            // 把宿主 API 指针作为整数 + 配置路径传给 Python。
            PyObject *args = Py_BuildValue(
                "(Ks)", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(s->host)),
                s->configPath.c_str());
            PyObject *r = PyObject_CallObject(f, args);
            Py_XDECREF(args);
            if (r != nullptr) {
                rc = PyObject_IsTrue(r) != 0 ? 1 : 0;
                Py_DECREF(r);
            } else {
                PyErr_Print();
            }
        }
        Py_XDECREF(f);
    } else {
        PyErr_Print();
    }
    PyGILState_Release(g);
    return rc;
}

int pyStart(void *self) { return callBool(static_cast<PyState *>(self), "start"); }
int pyStop(void *self) { return callBool(static_cast<PyState *>(self), "stop"); }

int pyDestroy(void *self) {
    auto *s = static_cast<PyState *>(self);
    callBool(s, "destroy");
    const PyGILState_STATE g = PyGILState_Ensure();
    Py_XDECREF(s->module);
    PyGILState_Release(g);
    delete s; // 在桥自身堆释放（与宿主 createPlugin 配对）
    // 注意：不调 Py_Finalize —— 解释器可能被多个 Python 插件共享，进程退出时统一回收。
    return 1;
}

// 取本 DLL 所在目录（用任一本模块内函数的地址定位自身）。
std::string selfDir() {
#if defined(_WIN32)
    HMODULE h = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&pyInit), &h);
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(h, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path().string();
#else
    Dl_info info{};
    if (::dladdr(reinterpret_cast<void *>(&pyInit), &info) != 0 && info.dli_fname != nullptr) {
        return std::filesystem::path(info.dli_fname).parent_path().string();
    }
    return ".";
#endif
}

} // namespace

#if defined(_WIN32)
#define PYHOST_EXPORT extern "C" __declspec(dllexport)
#else
#define PYHOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PYHOST_EXPORT IrPluginInfo getPluginInfo() {
    IrPluginInfo info{};
    info.abi_version = IRPLUGIN_ABI_VERSION;
    info.id = "py-s7";
    info.name = "Python S7 Driver (PyHost bridge)";
    info.version = "0.1.0";
    return info;
}

PYHOST_EXPORT int createPlugin(const IrPluginHostApi *host, const char *config_path,
                               IrPluginInstance *out) {
    if (host == nullptr || out == nullptr) {
        return 0;
    }
    // 首个 Python 插件负责初始化解释器；随后释放 GIL，后续全部经 PyGILState_Ensure 获取。
    // home 指向基础 Python，使嵌入解释器据此定位标准库（不依赖 python39.dll 的落点）。
    if (Py_IsInitialized() == 0) {
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
#ifdef PYHOST_PYTHON_HOME
        PyConfig_SetBytesString(&config, &config.home, PYHOST_PYTHON_HOME);
#endif
        const PyStatus st = Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);
        if (PyStatus_Exception(st) != 0) {
            return 0; // 解释器初始化失败
        }
        PyEval_SaveThread();
    }
    // 配置 import 搜索路径：venv 的 site-packages（snap7 在此）+ 本 DLL 目录（s7_driver.py 与 dll 同目录）。
    {
        const PyGILState_STATE g = PyGILState_Ensure();
        PyObject *sysPath = PySys_GetObject("path"); // 借用引用
        if (sysPath != nullptr) {
#ifdef PYHOST_VENV_SITE
            PyObject *v = PyUnicode_FromString(PYHOST_VENV_SITE);
            PyList_Insert(sysPath, 0, v);
            Py_DECREF(v);
#endif
            PyObject *d = PyUnicode_FromString(selfDir().c_str());
            PyList_Insert(sysPath, 0, d);
            Py_DECREF(d);
        }
        PyGILState_Release(g);
    }

    auto *s = new (std::nothrow) PyState{host, config_path != nullptr ? config_path : "", nullptr};
    if (s == nullptr) {
        return 0;
    }
    out->self = s;
    out->init = &pyInit;
    out->start = &pyStart;
    out->stop = &pyStop;
    out->destroy = &pyDestroy;
    return 1;
}
