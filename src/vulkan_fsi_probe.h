// vulkan_fsi_probe.h - whether this PC has what the Accurate render mode
// needs: a Vulkan device with fragment shader interlock and the other features
// the SDK's interlock render target path checks (VulkanRenderTargetCache::
// Initialize). Without them the game doesn't fail, it silently runs another
// path: no Vulkan - Direct3D 12 (automatic backend selection), no interlock -
// Vulkan host render targets, which don't emulate the EDRAM exactly. The
// launcher runs the probe on a worker thread when the settings open, to warn
// before the game restarts into Accurate. vulkan-1.dll is loaded at run time.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vulkan/vulkan_core.h>

namespace vp_launcher
{
  struct VulkanFsiProbe
  {
    enum class Result
    {
      kUnknown,      // could not check (Vulkan 1.0 loader), no warning
      kNoVulkan,     // no Vulkan driver or device
      kNoInterlock,  // Vulkan, but no device with the interlock features
      kSupported,
    };
    Result result = Result::kUnknown;
    std::string device;  // the supporting device, or the last one checked
  };

  // Checks the device the SDK will use (VulkanProvider::Create): device_index
  // (the vulkan_device setting) if it is valid; otherwise the first device by
  // the number of geometryShader, fragmentStoresAndAtomics,
  // vertexPipelineStoresAndAtomics and fillModeNonSolid, then the enumeration
  // order (the vulkan_prefer_* settings taken as their default, true). The SDK
  // also skips a device it fails to create, which is not repeated here.
  inline VulkanFsiProbe ProbeVulkanFsi(int32_t device_index)
  {
    using Result = VulkanFsiProbe::Result;
    VulkanFsiProbe probe;
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(L"vulkan-1.dll");
    if (!module)
    {
      probe.result = Result::kNoVulkan;
      return probe;
    }
    const auto get_proc =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module, "vkGetInstanceProcAddr"));
    const auto enumerate_version = get_proc ? reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                                                  get_proc(nullptr, "vkEnumerateInstanceVersion"))
                                            : nullptr;
    const auto create_instance =
        get_proc ? reinterpret_cast<PFN_vkCreateInstance>(get_proc(nullptr, "vkCreateInstance")) : nullptr;
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (enumerate_version)
    {
      enumerate_version(&loader_version);
    }
    VkInstance instance = VK_NULL_HANDLE;
    if (create_instance && loader_version >= VK_API_VERSION_1_1)
    {
      VkApplicationInfo app{};
      app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      app.pApplicationName = "vivapinata launcher";
      app.apiVersion = VK_API_VERSION_1_1;
      VkInstanceCreateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      info.pApplicationInfo = &app;
      if (create_instance(&info, nullptr, &instance) != VK_SUCCESS)
      {
        instance = VK_NULL_HANDLE;
        probe.result = Result::kNoVulkan;
      }
    }
    if (instance)
    {
      const auto destroy_instance =
          reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
      const auto enumerate_devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
          get_proc(instance, "vkEnumeratePhysicalDevices"));
      const auto get_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
          get_proc(instance, "vkGetPhysicalDeviceProperties"));
      const auto get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
          get_proc(instance, "vkGetPhysicalDeviceFeatures"));
      const auto get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
          get_proc(instance, "vkGetPhysicalDeviceFeatures2"));
      const auto enumerate_extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
          get_proc(instance, "vkEnumerateDeviceExtensionProperties"));
      std::vector<VkPhysicalDevice> devices;
      uint32_t count = 0;
      if (enumerate_devices && get_properties && get_features && get_features2 && enumerate_extensions &&
          enumerate_devices(instance, &count, nullptr) == VK_SUCCESS && count)
      {
        devices.resize(count);
        if (enumerate_devices(instance, &count, devices.data()) < VK_SUCCESS)
        {
          count = 0;
        }
        devices.resize(count);
      }
      if (device_index >= 0 && uint32_t(device_index) < devices.size())
      {
        devices = {devices[device_index]};
      }
      else if (devices.size() > 1)
      {
        auto score = [&](VkPhysicalDevice device) {
          VkPhysicalDeviceFeatures features{};
          get_features(device, &features);
          return uint32_t(features.geometryShader) + uint32_t(features.fragmentStoresAndAtomics) +
                 uint32_t(features.vertexPipelineStoresAndAtomics) + uint32_t(features.fillModeNonSolid);
        };
        std::vector<std::pair<uint32_t, VkPhysicalDevice>> scored;
        for (VkPhysicalDevice device : devices)
        {
          scored.emplace_back(score(device), device);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        devices = {scored.front().second};
      }
      probe.result = devices.empty() ? Result::kNoVulkan : Result::kNoInterlock;
      for (VkPhysicalDevice device : devices)
      {
        VkPhysicalDeviceProperties properties{};
        get_properties(device, &properties);
        probe.device = properties.deviceName;
        uint32_t extension_count = 0;
        enumerate_extensions(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        if (extension_count)
        {
          enumerate_extensions(device, nullptr, &extension_count, extensions.data());
          extensions.resize(extension_count);
        }
        const bool has_interlock_extension =
            std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties &e) {
              return std::strcmp(e.extensionName, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME) == 0;
            });
        if (!has_interlock_extension || properties.apiVersion < VK_API_VERSION_1_1)
        {
          continue;
        }
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{};
        interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &interlock;
        get_features2(device, &features);
        // REX_VK_DISABLE_FSI=1 makes the SDK treat interlock as unsupported
        // (research switch); the probe follows it, so the warning can be tested.
        const char *disable_fsi = std::getenv("REX_VK_DISABLE_FSI");
        if ((interlock.fragmentShaderSampleInterlock || interlock.fragmentShaderPixelInterlock) &&
            features.features.fragmentStoresAndAtomics && features.features.sampleRateShading &&
            properties.limits.standardSampleLocations &&
            !(disable_fsi && disable_fsi[0] && disable_fsi[0] != '0'))
        {
          probe.result = Result::kSupported;
          break;
        }
      }
      if (destroy_instance)
      {
        destroy_instance(instance, nullptr);
      }
    }
    FreeLibrary(module);
#else
    (void)device_index;
#endif
    return probe;
  }
}  // namespace vp_launcher
