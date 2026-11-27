#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/FrameUniforms.hpp"
#include "core/Observer.hpp"
#include "core/World.hpp"

// [0단계 & 4~9단계: Vulkan 백엔드 렌더러]
class VulkanCubeRenderer {
public:
    // [9단계: CPU↔GPU 프레임 자원 격리 (In-flight Frame Isolation)]
    // GPU↔Display 출력 버퍼 격리인 스왑체인 이미지 풀과는 별개 계층
    static constexpr std::size_t kMaxFramesInFlight = 2;

    using SurfaceFactory = std::function<VkSurfaceKHR(VkInstance)>;

    // [0단계 Bootstrap: 실행 시 1회 정적 환경 계약(0.2~0.8) 일괄 체결]
    VulkanCubeRenderer(
        const std::vector<const char*>& platformInstanceExtensions,
        SurfaceFactory createSurface,
        std::uint32_t initialWidth,
        std::uint32_t initialHeight,
        const std::filesystem::path& shaderDir
    )
        : _pendingWidth(initialWidth),
          _pendingHeight(initialHeight),
          _shaderDir(shaderDir) {
        try {
            createInstance(platformInstanceExtensions);
            _surface = createSurface(_instance);
            if (_surface == VK_NULL_HANDLE) {
                throw std::runtime_error("Failed to create Vulkan surface.");
            }
            pickPhysicalDevice();
            createLogicalDevice();
            createCommandPool();
            createSwapchain();
            createSwapchainImageViews();
            createDepthResources();
            createRenderPass();
            createDescriptorSetLayout();
            createGraphicsPipeline();
            createFramebuffers();
            createGeometryBuffers();
            createUniformBuffers();
            createDescriptorPoolAndSets();
            createCommandBuffers();
            createSyncObjects();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    // [3.2 Teardown: GPU 작업 완료 대기 후 모든 자원과 표면 계약을 역순 해제]
    ~VulkanCubeRenderer() {
        cleanup();
    }

    VulkanCubeRenderer(const VulkanCubeRenderer&) = delete;
    VulkanCubeRenderer& operator=(const VulkanCubeRenderer&) = delete;
    VulkanCubeRenderer(VulkanCubeRenderer&&) = delete;
    VulkanCubeRenderer& operator=(VulkanCubeRenderer&&) = delete;

    // [3.2 Renegotiation: OS 창 크기 변경 통지 수신]
    void notifyFramebufferResized(std::uint32_t width, std::uint32_t height) noexcept {
        _pendingWidth = width;
        _pendingHeight = height;
        _framebufferResized = true;
    }

    // [2~9단계: 런타임 프레임 루프 — 동기화, 변환 행렬 갱신, 명령 기록·제출, 출력 표출]
    void renderFrame(const WorldState& world, const Observer& observer) {
        if (_pendingWidth == 0 || _pendingHeight == 0) {
            return;
        }

        // [3.2 Renegotiation: 창 크기 변경에 따른 스왑체인·깊이 버퍼 재협상]
        if (_framebufferResized) {
            _framebufferResized = false;
            recreateSwapchain();
            if (_swapchainExtent.width == 0 || _swapchainExtent.height == 0) {
                return;
            }
        }

        // [9단계: CPU↔GPU 프레임 자원 격리 — 현재 슬롯의 이전 GPU 작업 완료 대기]
        FrameSync& frame = _frames[_currentFrame];
        checkVk(vkWaitForFences(_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences failed");

        // [9단계: GPU↔Display 출력 버퍼 격리 — 다음 스왑체인 이미지 획득]
        std::uint32_t imageIndex = 0;
        VkResult acquireResult = vkAcquireNextImageKHR(
            _device,
            _swapchain,
            UINT64_MAX,
            frame.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

        float aspectRatio = (_swapchainExtent.height > 0)
                                ? static_cast<float>(_swapchainExtent.width) /
                                      static_cast<float>(_swapchainExtent.height)
                                : 1.0f;

        // [2~4단계 & 0.5단계: 세계·시점·사영 행렬 및 광원 방향을 CPU↔GPU ABI 버퍼에 기록]
        FrameUniforms uniforms{};
        uniforms.modelMatrix = world.computeModelMatrix();
        uniforms.viewMatrix = observer.computeViewMatrix();
        // [4단계: Vulkan의 +Y down NDC 규약 보정을 위해 flipY=true 적용]
        // 코어 큐브 인덱스는 반시계(CCW)이나 flipY로 화면 공간 감기 방향이 반전되므로
        // 파이프라인에서 VK_FRONT_FACE_CLOCKWISE를 앞면으로 지정합니다.
        uniforms.projectionMatrix = observer.computeProjectionMatrix(aspectRatio, true);
        uniforms.lightDirWorld = world.lightDirectionWorld();

        std::memcpy(frame.uniformMapped, &uniforms, sizeof(FrameUniforms));

        // [0.3단계 & 4~8단계: 현재 슬롯의 명령 버퍼 초기화 및 렌더링 명령 기록]
        checkVk(vkResetFences(_device, 1, &frame.inFlightFence), "vkResetFences failed");
        checkVk(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");
        recordCommandBuffer(frame.commandBuffer, frame.descriptorSet, imageIndex);

        // [0.3단계 & 9단계: 비동기 그래픽스 큐 제출 및 획득/완료 세마포어·펜스 동기화]
        VkSemaphore waitSemaphores[] = { frame.imageAvailableSemaphore };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { _renderFinishedSemaphores[imageIndex] };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        checkVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, frame.inFlightFence),
                "vkQueueSubmit failed");

        // [9단계: GPU↔Display 출력 표출(Presentation) 및 표면 무효화 시 3.2 Renegotiation]
        VkSwapchainKHR swapchains[] = { _swapchain };
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        VkResult presentResult = vkQueuePresentKHR(_presentQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            _framebufferResized) {
            _framebufferResized = false;
            recreateSwapchain();
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("vkQueuePresentKHR failed");
        }

        _currentFrame = (_currentFrame + 1) % kMaxFramesInFlight;
    }

private:
    struct QueueFamilyIndices {
        std::optional<std::uint32_t> graphicsFamily;
        std::optional<std::uint32_t> presentFamily;

        bool isComplete() const noexcept {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct FrameSync {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
        VkBuffer uniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        void* uniformMapped = nullptr;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    static void checkVk(VkResult result, const char* message) {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::string(message) + " (VkResult=" + std::to_string(result) + ")");
        }
    }

    // [0.2단계: Vulkan 인스턴스 생성 및 플랫폼 표면 확장 계약]
    void createInstance(const std::vector<const char*>& platformExtensions) {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Semantic 3D Cube";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "None";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(platformExtensions.size());
        createInfo.ppEnabledExtensionNames = platformExtensions.data();

        checkVk(vkCreateInstance(&createInfo, nullptr, &_instance), "vkCreateInstance failed");
    }

    // [0.3단계: 그래픽스 연산 및 화면 표출(Present) 지원 큐 패밀리 탐색]
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilyIndices indices;

        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        for (std::uint32_t i = 0; i < count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                indices.presentFamily = i;
            }
            if (indices.isComplete()) {
                break;
            }
        }

        return indices;
    }

    // [0.2단계 & 0.4단계: 물리 GPU의 스왑체인 출력 확장 지원 여부 검증]
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const {
        std::uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        for (const auto& ext : available) {
            if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                return true;
            }
        }
        return false;
    }

    // [0.4단계: 물리 GPU와 OS 표면 간 해상도·색 공간·표출 모드 능력 질의]
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) const {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);
        if (formatCount > 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.formats.data());
        }

        std::uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);
        if (presentModeCount > 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    // [0.2단계: 연산 주체 탐색 — 그래픽스·표출·스왑체인 요건을 만족하는 물리 GPU 선정]
    void pickPhysicalDevice() {
        std::uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("No Vulkan-compatible physical devices found.");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

        VkPhysicalDevice fallbackDevice = VK_NULL_HANDLE;

        for (VkPhysicalDevice dev : devices) {
            QueueFamilyIndices indices = findQueueFamilies(dev);
            if (!indices.isComplete() || !checkDeviceExtensionSupport(dev)) {
                continue;
            }
            SwapchainSupportDetails swapchainSupport = querySwapchainSupport(dev);
            if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) {
                continue;
            }

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                _physicalDevice = dev;
                _queueFamilies = indices;
                return;
            }
            if (fallbackDevice == VK_NULL_HANDLE) {
                fallbackDevice = dev;
            }
        }

        if (fallbackDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to find a suitable Vulkan GPU.");
        }

        _physicalDevice = fallbackDevice;
        _queueFamilies = findQueueFamilies(_physicalDevice);
    }

    // [0.2단계 & 0.3단계: 논리 장치(Device) 점유 및 그래픽스·표출 비동기 명령 큐 획득]
    void createLogicalDevice() {
        std::set<std::uint32_t> uniqueFamilies = {
            _queueFamilies.graphicsFamily.value(),
            _queueFamilies.presentFamily.value()
        };

        float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());
        for (std::uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        std::array<const char*, 1> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        checkVk(vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device),
                "vkCreateDevice failed");

        vkGetDeviceQueue(_device, _queueFamilies.graphicsFamily.value(), 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, _queueFamilies.presentFamily.value(), 0, &_presentQueue);
    }

    // [0.3단계: 비동기 명령 버퍼 할당용 커맨드 풀 개설]
    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = _queueFamilies.graphicsFamily.value();

        checkVk(vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool),
                "vkCreateCommandPool failed");
    }

    // [0.4단계 & 8단계: sRGB 비선형 색 공간 출력 포맷 선정 (저장 시 하드웨어 감마 인코딩)]
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return fmt;
            }
        }
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_R8G8B8A8_SRGB &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return fmt;
            }
        }
        return formats.front();
    }

    // [0.4단계 & 9단계: 주사 동기화 정책 선정 (Mailbox 우선, 미지원 시 FIFO V-Sync)]
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // [0.4단계: OS 표면 제약 범위 내 출력 해상도 합의]
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const {
        if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return caps.currentExtent;
        }
        VkExtent2D actual{ _pendingWidth, _pendingHeight };
        actual.width = std::clamp(actual.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return actual;
    }

    // [0.6단계 & 6단계: 폐색 해결용 깊이 버퍼 포맷 선정]
    VkFormat findDepthFormat() const {
        constexpr std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };
        for (VkFormat fmt : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(_physicalDevice, fmt, &props);
            if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return fmt;
            }
        }
        throw std::runtime_error("Failed to find a supported depth format.");
    }

    // [0.6단계: 하드웨어 메모리 토폴로지에서 요구 속성에 맞는 메모리 힙 탐색]
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProps);
        for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable Vulkan memory type.");
    }

    // [0.4단계: OS 디스플레이 표면 결합 및 출력 순환(Swapchain) 구축]
    void createSwapchain() {
        SwapchainSupportDetails support = querySwapchainSupport(_physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
        VkExtent2D extent = chooseSwapExtent(support.capabilities);

        std::uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = _surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        std::uint32_t queueFamilyIndices[] = {
            _queueFamilies.graphicsFamily.value(),
            _queueFamilies.presentFamily.value()
        };
        if (_queueFamilies.graphicsFamily != _queueFamilies.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        checkVk(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain),
                "vkCreateSwapchainKHR failed");

        std::uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(_device, _swapchain, &actualCount, nullptr);
        _swapchainImages.resize(actualCount);
        vkGetSwapchainImagesKHR(_device, _swapchain, &actualCount, _swapchainImages.data());

        _swapchainImageFormat = surfaceFormat.format;
        _swapchainExtent = extent;
    }

    // [0.4단계: 스왑체인 색상 이미지를 렌더 타겟 뷰로 래핑]
    void createSwapchainImageViews() {
        _swapchainImageViews.resize(_swapchainImages.size(), VK_NULL_HANDLE);
        for (std::size_t i = 0; i < _swapchainImages.size(); ++i) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = _swapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = _swapchainImageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            checkVk(vkCreateImageView(_device, &viewInfo, nullptr, &_swapchainImageViews[i]),
                    "vkCreateImageView failed");
        }
    }

    // [0.6단계 & 6단계: GPU 전용(Device-local) 깊이 버퍼 자원 실체화]
    void createDepthResources() {
        _depthFormat = findDepthFormat();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = _swapchainExtent.width;
        imageInfo.extent.height = _swapchainExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = _depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        checkVk(vkCreateImage(_device, &imageInfo, nullptr, &_depthImage),
                "vkCreateImage (depth) failed");

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(_device, _depthImage, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        checkVk(vkAllocateMemory(_device, &allocInfo, nullptr, &_depthImageMemory),
                "vkAllocateMemory (depth) failed");
        checkVk(vkBindImageMemory(_device, _depthImage, _depthImageMemory, 0),
                "vkBindImageMemory (depth) failed");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = _depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = _depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        checkVk(vkCreateImageView(_device, &viewInfo, nullptr, &_depthImageView),
                "vkCreateImageView (depth) failed");
    }

    // [0.7단계 & 0.8단계: 렌더 패스 계약 — 색상/깊이 어태치먼트, 레이아웃 전환 및 서브패스 의존성]
    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = _swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = _depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        checkVk(vkCreateRenderPass(_device, &rpInfo, nullptr, &_renderPass),
                "vkCreateRenderPass failed");
    }

    // [0.5단계: CPU↔GPU 데이터 ABI — FrameUniforms 유니폼 버퍼 바인딩 규약]
    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        checkVk(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout),
                "vkCreateDescriptorSetLayout failed");
    }

    static std::vector<char> readBinaryFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + path.string());
        }
        std::streamsize size = file.tellg();
        if (size <= 0) {
            throw std::runtime_error("Shader file is empty: " + path.string());
        }
        std::vector<char> buffer(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(buffer.data(), size);
        return buffer;
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(_device, &createInfo, nullptr, &module),
                "vkCreateShaderModule failed");
        return module;
    }

    // [0.7단계: 불변 실행 상태(PSO) 사전 컴파일 — SPIR-V 셰이더, 정점 ABI, 래스터·깊이·블렌드 상태 고정]
    void createGraphicsPipeline() {
        auto vertCode = readBinaryFile(_shaderDir / "cube.vert.spv");
        auto fragCode = readBinaryFile(_shaderDir / "cube.frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

        // [0.5단계: 정점 버퍼 스트라이드 및 속성(위치·법선·색상) 오프셋 ABI]
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(Vertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrDescs{};
        attrDescs[0].binding = 0;
        attrDescs[0].location = 0;
        attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDescs[0].offset = static_cast<std::uint32_t>(offsetof(Vertex, position));

        attrDescs[1].binding = 0;
        attrDescs[1].location = 1;
        attrDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDescs[1].offset = static_cast<std::uint32_t>(offsetof(Vertex, normal));

        attrDescs[2].binding = 0;
        attrDescs[2].location = 2;
        attrDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDescs[2].offset = static_cast<std::uint32_t>(offsetof(Vertex, color));

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrDescs.size());
        vertexInput.pVertexAttributeDescriptions = attrDescs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // [0.7단계: 창 크기 변경 시 파이프라인 재컴파일을 피하기 위한 동적 뷰포트·시저 분리]
        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // [4~5단계 경계: 후면 제거 (Back-face Culling) 및 5단계 이산화 설정]
        // 코어 큐브 인덱스는 반시계(CCW)이지만, Vulkan의 +Y down NDC 보정(flipY=true)으로
        // 화면 공간 정점 감기 방향이 반전되므로 VK_FRONT_FACE_CLOCKWISE를 앞면으로 지정합니다.
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        // [6단계: 폐색 해결 — 깊이 비교(LESS) 및 깊이 쓰기 활성화]
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // [8단계: 불투명 단일 표면 출력 — 블렌딩 비활성화]
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                              VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT |
                                              VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &_descriptorSetLayout;

        VkResult layoutRes = vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_pipelineLayout);
        if (layoutRes != VK_SUCCESS) {
            vkDestroyShaderModule(_device, fragModule, nullptr);
            vkDestroyShaderModule(_device, vertModule, nullptr);
            checkVk(layoutRes, "vkCreatePipelineLayout failed");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = _pipelineLayout;
        pipelineInfo.renderPass = _renderPass;
        pipelineInfo.subpass = 0;

        VkResult pipeRes = vkCreateGraphicsPipelines(
            _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_graphicsPipeline
        );

        vkDestroyShaderModule(_device, fragModule, nullptr);
        vkDestroyShaderModule(_device, vertModule, nullptr);
        checkVk(pipeRes, "vkCreateGraphicsPipelines failed");
    }

    // [0.4단계 & 0.6단계: 스왑체인 색상 뷰와 깊이 뷰를 결합한 프레임버퍼 생성]
    void createFramebuffers() {
        _framebuffers.resize(_swapchainImageViews.size(), VK_NULL_HANDLE);
        for (std::size_t i = 0; i < _swapchainImageViews.size(); ++i) {
            std::array<VkImageView, 2> attachments = {
                _swapchainImageViews[i],
                _depthImageView
            };

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = _renderPass;
            fbInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            fbInfo.pAttachments = attachments.data();
            fbInfo.width = _swapchainExtent.width;
            fbInfo.height = _swapchainExtent.height;
            fbInfo.layers = 1;

            checkVk(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_framebuffers[i]),
                    "vkCreateFramebuffer failed");
        }
    }

    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    ) const {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        checkVk(vkCreateBuffer(_device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer failed");

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(_device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);

        checkVk(vkAllocateMemory(_device, &allocInfo, nullptr, &memory), "vkAllocateMemory failed");
        checkVk(vkBindBufferMemory(_device, buffer, memory, 0), "vkBindBufferMemory failed");
    }

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = _commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        checkVk(vkAllocateCommandBuffers(_device, &allocInfo, &cmd), "vkAllocateCommandBuffers (copy) failed");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        checkVk(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer (copy) failed");

        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);

        checkVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (copy) failed");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        checkVk(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit (copy) failed");
        checkVk(vkQueueWaitIdle(_graphicsQueue), "vkQueueWaitIdle (copy) failed");

        vkFreeCommandBuffers(_device, _commandPool, 1, &cmd);
    }

    // [0.6단계: 스테이징 버퍼를 경유한 전송으로 Device-local VRAM 자원 실체화]
    void createDeviceLocalBuffer(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkBuffer& dstBuffer,
        VkDeviceMemory& dstMemory
    ) const {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        );

        void* mapped = nullptr;
        checkVk(vkMapMemory(_device, stagingMemory, 0, size, 0, &mapped), "vkMapMemory (staging) failed");
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        vkUnmapMemory(_device, stagingMemory);

        createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            dstBuffer,
            dstMemory
        );

        copyBuffer(stagingBuffer, dstBuffer, size);

        vkDestroyBuffer(_device, stagingBuffer, nullptr);
        vkFreeMemory(_device, stagingMemory, nullptr);
    }

    // [0.6단계 & 1단계: 정적 개체 기하(정점·인덱스)를 GPU 전용 메모리에 적재]
    void createGeometryBuffers() {
        createDeviceLocalBuffer(
            kCubeVertices.data(),
            sizeof(kCubeVertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            _vertexBuffer,
            _vertexMemory
        );
        createDeviceLocalBuffer(
            kCubeIndices.data(),
            sizeof(kCubeIndices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            _indexBuffer,
            _indexMemory
        );
    }

    // [0.6단계 & 9단계: 프레임 슬롯별 Host-visible 영구 매핑 유니폼 버퍼 할당]
    void createUniformBuffers() {
        for (auto& frame : _frames) {
            createBuffer(
                sizeof(FrameUniforms),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                frame.uniformBuffer,
                frame.uniformMemory
            );
            checkVk(vkMapMemory(_device, frame.uniformMemory, 0, sizeof(FrameUniforms), 0, &frame.uniformMapped),
                    "vkMapMemory (uniform) failed");
        }
    }

    // [0.5단계 & 9단계: 프레임 슬롯별 디스크립터 셋 할당 및 유니폼 버퍼 바인딩]
    void createDescriptorPoolAndSets() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<std::uint32_t>(kMaxFramesInFlight);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<std::uint32_t>(kMaxFramesInFlight);

        checkVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool),
                "vkCreateDescriptorPool failed");

        std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
        layouts.fill(_descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = _descriptorPool;
        allocInfo.descriptorSetCount = static_cast<std::uint32_t>(kMaxFramesInFlight);
        allocInfo.pSetLayouts = layouts.data();

        std::array<VkDescriptorSet, kMaxFramesInFlight> sets{};
        checkVk(vkAllocateDescriptorSets(_device, &allocInfo, sets.data()),
                "vkAllocateDescriptorSets failed");

        for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
            _frames[i].descriptorSet = sets[i];

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = _frames[i].uniformBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(FrameUniforms);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = _frames[i].descriptorSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
        }
    }

    // [0.3단계 & 9단계: 프레임 슬롯별 주 명령 버퍼(Primary Command Buffer) 할당]
    void createCommandBuffers() {
        std::array<VkCommandBuffer, kMaxFramesInFlight> buffers{};
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<std::uint32_t>(kMaxFramesInFlight);

        checkVk(vkAllocateCommandBuffers(_device, &allocInfo, buffers.data()),
                "vkAllocateCommandBuffers failed");

        for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
            _frames[i].commandBuffer = buffers[i];
        }
    }

    // [0.8단계 & 9단계: 스왑체인 이미지별 렌더 완료 -> 표출 대기 GPU 세마포어 생성]
    void createRenderFinishedSemaphores() {
        _renderFinishedSemaphores.resize(_swapchainImages.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (auto& sem : _renderFinishedSemaphores) {
            checkVk(vkCreateSemaphore(_device, &semInfo, nullptr, &sem),
                    "vkCreateSemaphore (renderFinished) failed");
        }
    }

    // [0.8단계: CPU↔GPU 펜스 및 GPU 내부 획득·표출 세마포어 동기화 원시체 구축]
    void createSyncObjects() {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (auto& frame : _frames) {
            checkVk(vkCreateSemaphore(_device, &semInfo, nullptr, &frame.imageAvailableSemaphore),
                    "vkCreateSemaphore (imageAvailable) failed");
            checkVk(vkCreateFence(_device, &fenceInfo, nullptr, &frame.inFlightFence),
                    "vkCreateFence failed");
        }

        createRenderFinishedSemaphores();
    }

    // [4~8단계: 렌더 패스 명령 기록 — 뷰포트·시저, 기하·유니폼 바인딩, 인덱스 드로우]
    void recordCommandBuffer(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, std::uint32_t imageIndex) const {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        checkVk(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer failed");

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{ 0.08f, 0.10f, 0.14f, 1.0f }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = _renderPass;
        rpBegin.framebuffer = _framebuffers[imageIndex];
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = _swapchainExtent;
        rpBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicsPipeline);

        // [5단계: NDC -> 화면 공간(Screen Space) 뷰포트 변환 및 시저 영역 설정]
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(_swapchainExtent.width);
        viewport.height = static_cast<float>(_swapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = _swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = { _vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, _indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _pipelineLayout,
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );

        // [4~8단계: 삼각형 조립 -> 투영 -> 이산화 -> 깊이 판별 -> 광학 계산 -> sRGB 출력]
        vkCmdDrawIndexed(cmd, static_cast<std::uint32_t>(kCubeIndices.size()), 1, 0, 0, 0);

        vkCmdEndRenderPass(cmd);
        checkVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer failed");
    }

    // [3.2 Renegotiation / Teardown: 표면 종속 자원(프레임버퍼·깊이·스왑체인) 파기]
    void cleanupSwapchain() {
        for (VkFramebuffer fb : _framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(_device, fb, nullptr);
            }
        }
        _framebuffers.clear();

        if (_depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(_device, _depthImageView, nullptr);
            _depthImageView = VK_NULL_HANDLE;
        }
        if (_depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(_device, _depthImage, nullptr);
            _depthImage = VK_NULL_HANDLE;
        }
        if (_depthImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(_device, _depthImageMemory, nullptr);
            _depthImageMemory = VK_NULL_HANDLE;
        }

        for (VkImageView view : _swapchainImageViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(_device, view, nullptr);
            }
        }
        _swapchainImageViews.clear();

        for (VkSemaphore sem : _renderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(_device, sem, nullptr);
            }
        }
        _renderFinishedSemaphores.clear();

        if (_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(_device, _swapchain, nullptr);
            _swapchain = VK_NULL_HANDLE;
        }
        _swapchainImages.clear();
    }

    // [3.2 Renegotiation: 창 크기 변경 또는 표면 무효화 시 출력 순환 및 깊이 자원 재협상]
    void recreateSwapchain() {
        if (_pendingWidth == 0 || _pendingHeight == 0) {
            _framebufferResized = true;
            return;
        }
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface, &caps) == VK_SUCCESS) {
            VkExtent2D extent = chooseSwapExtent(caps);
            if (extent.width == 0 || extent.height == 0) {
                _framebufferResized = true;
                return;
            }
        }
        vkDeviceWaitIdle(_device);
        cleanupSwapchain();
        createSwapchain();
        createSwapchainImageViews();
        createDepthResources();
        createFramebuffers();
        createRenderFinishedSemaphores();
    }

    // [3.2 Teardown: GPU 유휴 대기 후 모든 Vulkan 자원과 OS 표면 계약 역순 해제]
    void cleanup() noexcept {
        if (_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(_device);

            cleanupSwapchain();

            for (auto& frame : _frames) {
                if (frame.inFlightFence != VK_NULL_HANDLE) {
                    vkDestroyFence(_device, frame.inFlightFence, nullptr);
                    frame.inFlightFence = VK_NULL_HANDLE;
                }
                if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(_device, frame.imageAvailableSemaphore, nullptr);
                    frame.imageAvailableSemaphore = VK_NULL_HANDLE;
                }
                if (frame.uniformMapped != nullptr) {
                    vkUnmapMemory(_device, frame.uniformMemory);
                    frame.uniformMapped = nullptr;
                }
                if (frame.uniformBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(_device, frame.uniformBuffer, nullptr);
                    frame.uniformBuffer = VK_NULL_HANDLE;
                }
                if (frame.uniformMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(_device, frame.uniformMemory, nullptr);
                    frame.uniformMemory = VK_NULL_HANDLE;
                }
            }

            if (_graphicsPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(_device, _graphicsPipeline, nullptr);
                _graphicsPipeline = VK_NULL_HANDLE;
            }
            if (_pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
                _pipelineLayout = VK_NULL_HANDLE;
            }
            if (_renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(_device, _renderPass, nullptr);
                _renderPass = VK_NULL_HANDLE;
            }
            if (_descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
                _descriptorPool = VK_NULL_HANDLE;
            }
            if (_descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
                _descriptorSetLayout = VK_NULL_HANDLE;
            }
            if (_indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(_device, _indexBuffer, nullptr);
                _indexBuffer = VK_NULL_HANDLE;
            }
            if (_indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(_device, _indexMemory, nullptr);
                _indexMemory = VK_NULL_HANDLE;
            }
            if (_vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(_device, _vertexBuffer, nullptr);
                _vertexBuffer = VK_NULL_HANDLE;
            }
            if (_vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(_device, _vertexMemory, nullptr);
                _vertexMemory = VK_NULL_HANDLE;
            }
            if (_commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(_device, _commandPool, nullptr);
                _commandPool = VK_NULL_HANDLE;
            }

            vkDestroyDevice(_device, nullptr);
            _device = VK_NULL_HANDLE;
        }

        if (_surface != VK_NULL_HANDLE && _instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(_instance, _surface, nullptr);
            _surface = VK_NULL_HANDLE;
        }
        if (_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(_instance, nullptr);
            _instance = VK_NULL_HANDLE;
        }
    }

    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;

    QueueFamilyIndices _queueFamilies{};
    VkQueue _graphicsQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;

    VkCommandPool _commandPool = VK_NULL_HANDLE;

    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkFormat _swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent{};

    VkFormat _depthFormat = VK_FORMAT_UNDEFINED;
    VkImage _depthImage = VK_NULL_HANDLE;
    VkDeviceMemory _depthImageMemory = VK_NULL_HANDLE;
    VkImageView _depthImageView = VK_NULL_HANDLE;

    VkRenderPass _renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> _framebuffers;

    VkBuffer _vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _vertexMemory = VK_NULL_HANDLE;
    VkBuffer _indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _indexMemory = VK_NULL_HANDLE;

    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    std::array<FrameSync, kMaxFramesInFlight> _frames{};
    std::vector<VkSemaphore> _renderFinishedSemaphores;

    std::size_t _currentFrame = 0;
    std::uint32_t _pendingWidth = 0;
    std::uint32_t _pendingHeight = 0;
    bool _framebufferResized = false;
    std::filesystem::path _shaderDir;
};
