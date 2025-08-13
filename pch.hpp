#pragma once

#define VC_EXTRALEAN
#define NOMINMAX            // Prevents global min/max macros
#define NOSERVICE           // Removes Windows Service APIs
#define NOHELP              // Removes Help engine APIs
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>
#include <userenv.h>
#include <shlobj.h>
#include <d2d1.h>
#include <dwmapi.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <optional>
#include <string>
#include <vector>