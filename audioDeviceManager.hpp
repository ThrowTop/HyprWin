#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <atlbase.h>
#include <string>
#include <vector>
#include <mmreg.h>

#include "utils.hpp"

class AudioDeviceManager {
private:
    AudioDeviceManager() {
        (void)CLSIDFromString(L"{870af99c-171d-4f9e-af0d-e63df40c2bc9}", &policyClsid);
        updateDevices();
        currentId = getCurrentDefaultRenderDeviceId();
    }

    AudioDeviceManager(const AudioDeviceManager&) = delete;
    AudioDeviceManager& operator=(const AudioDeviceManager&) = delete;
    AudioDeviceManager(AudioDeviceManager&&) = delete;
    AudioDeviceManager& operator=(AudioDeviceManager&&) = delete;

public:
    struct AudioDeviceInfo {
        std::wstring name;
        std::wstring id;
    };

    static AudioDeviceManager& Instance() {
        static AudioDeviceManager instance;
        return instance;
    }

    const std::vector<AudioDeviceInfo>& getAllRenderDevices() const {
        return devices;
    }

    const std::wstring& getCurrentDeviceId() const {
        return currentId;
    }

    const std::wstring& getCurrentDeviceName() const {
        return currentName;
    }

    void updateDevices() {
        devices.clear();

        CComPtr<IMMDeviceEnumerator> enumerator;
        CComPtr<IMMDeviceCollection> collection;

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
            return;
        }

        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
            return;
        }

        UINT count = 0;
        collection->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            CComPtr<IMMDevice> device;
            if (FAILED(collection->Item(i, &device))) continue;

            CComHeapPtr<WCHAR> id;
            device->GetId(&id);

            CComPtr<IPropertyStore> props;
            if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) continue;

            PROPVARIANT nameProp;
            PropVariantInit(&nameProp);
            props->GetValue(PKEY_Device_FriendlyName, &nameProp);

            std::wstring name = nameProp.pwszVal ? nameProp.pwszVal : L"";
            PropVariantClear(&nameProp);

            devices.push_back({ name, std::wstring(id) });
        }

        auto id = getCurrentDefaultRenderDeviceId();
        currentId = id;
        currentName.clear();

        if (!id.empty()) {
            auto it = std::find_if(devices.begin(), devices.end(), [&](const auto& dev) {
                return dev.id == id;
            });
            if (it != devices.end()) currentName = it->name;
        }
    }

    bool setDefaultDevice(const std::wstring& deviceId) const {
        CComPtr<IPolicyConfig> policyConfig;
        HRESULT hr = CoCreateInstance(policyClsid, nullptr, CLSCTX_ALL,
            __uuidof(IPolicyConfig), (void**)&policyConfig);
        if (FAILED(hr)) {
            WLOG(L"CoCreateInstance failed: 0x%08X", hr);
            return false;
        }

        hr = policyConfig->SetDefaultEndpoint(deviceId.c_str(), 0);
        if (FAILED(hr)) {
            WLOG(L"SetDefaultEndpoint (console) failed: 0x%08X", hr);
            return false;
        }

        hr = policyConfig->SetDefaultEndpoint(deviceId.c_str(), 2);
        if (FAILED(hr)) {
            WLOG(L"SetDefaultEndpoint (comm) failed: 0x%08X", hr);
            return false;
        }

        return true;
    }

    bool cycleToNextDevice() {
        updateDevices();
        if (currentId.empty() || devices.empty()) return false;

        auto it = std::find_if(devices.begin(), devices.end(), [&](const auto& dev) {
            return dev.id == currentId;
        });
        int currentIndex = it != devices.end() ? static_cast<int>(std::distance(devices.begin(), it)) : -1;

        int nextIndex = (currentIndex + 1) % devices.size();
        const auto& nextDevice = devices[nextIndex];

        if (setDefaultDevice(nextDevice.id)) {
            WLOG(L"Switched to: %s", nextDevice.name.c_str());
            currentId = nextDevice.id;
            currentName = nextDevice.name;
            return true;
        }

        WLOG(L"Failed to switch to: %s", nextDevice.name.c_str());
        return false;
    }

    bool cycleToNextMatchingDevice(const std::vector<std::wstring>& patterns) {
        updateDevices();
        int currentIndex = -1;

        for (int i = 0; i < static_cast<int>(patterns.size()); ++i) {
            if (!currentName.empty() && currentName.find(patterns[i]) != std::wstring::npos) {
                currentIndex = i;
                break;
            }
        }

        int nextIndex = (currentIndex + 1) % patterns.size();
        const std::wstring& targetPattern = patterns[nextIndex];

        for (const auto& dev : devices) {
            if (dev.name.find(targetPattern) != std::wstring::npos) {
                if (setDefaultDevice(dev.id)) {
                    WLOG(L"Switched to: %s", dev.name.c_str());
                    currentId = dev.id;
                    currentName = dev.name;
                    return true;
                }
                else {
                    WLOG(L"Failed to set device: %s", dev.name.c_str());
                    return false;
                }
            }
        }

        WLOG(L"No matching device found for pattern: %s", targetPattern.c_str());
        return false;
    }

private:
    CLSID policyClsid;
    std::vector<AudioDeviceInfo> devices;
    std::wstring currentId;
    std::wstring currentName;

    struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig;
    struct IPolicyConfig : public IUnknown {
        virtual HRESULT STDMETHODCALLTYPE GetMixFormat(LPCWSTR, WAVEFORMATEX**) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(LPCWSTR, INT, WAVEFORMATEX**) = 0;
        virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(LPCWSTR) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(LPCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(LPCWSTR, INT, PINT64, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(LPCWSTR, PINT64) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetShareMode(LPCWSTR, struct DeviceShareMode*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetShareMode(LPCWSTR, struct DeviceShareMode*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(LPCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(LPCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR wszDeviceId, int role) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(LPCWSTR, INT) = 0;
    };

    std::wstring getCurrentDefaultRenderDeviceId() const {
        CComPtr<IMMDeviceEnumerator> enumerator;
        CComPtr<IMMDevice> defaultDevice;
        CComHeapPtr<WCHAR> defaultId;

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
            return L"";
        }

        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            return L"";
        }

        if (FAILED(defaultDevice->GetId(&defaultId))) {
            return L"";
        }

        return std::wstring(defaultId);
    }
};