/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// INTERNAL INCLUDES
#include <dali/graphics/vulkan/vulkan-graphics.h>
#include <dali/graphics/vulkan/vulkan-command-pool.h>
#include <dali/graphics/vulkan/vulkan-queue.h>
#include <dali/graphics/vulkan/vulkan-surface.h>
#include <dali/integration-api/graphics/vulkan/vk-surface-factory.h>
#include <dali/graphics/vulkan/gpu-memory/vulkan-gpu-memory-manager.h>

#include <dali/graphics/vulkan/vulkan-buffer.h>
#include <dali/graphics/vulkan/vulkan-image.h>
#include <dali/graphics/vulkan/vulkan-pipeline.h>
#include <dali/graphics/vulkan/vulkan-shader.h>
#include <dali/graphics/vulkan/vulkan-descriptor-set.h>
#include <dali/graphics/vulkan/vulkan-framebuffer.h>
#include <dali/graphics/vulkan/api/vulkan-api-controller.h>
#include <dali/graphics/vulkan/vulkan-pipeline-cache.h>
#include <dali/graphics/vulkan/vulkan-sampler.h>
#include <dali/graphics/vulkan/vulkan-resource-cache.h>
#include <dali/graphics/vulkan/vulkan-debug.h>
#include <dali/graphics-api/graphics-api-controller.h>

#ifndef VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#define VK_KHR_XLIB_SURFACE_EXTENSION_NAME "VK_KHR_xlib_surface"
#endif

#ifndef VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
#define VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME "VK_KHR_wayland_surface"
#endif

#ifndef VK_KHR_XCB_SURFACE_EXTENSION_NAME
#define VK_KHR_XCB_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#endif

namespace Dali
{
namespace Graphics
{
using VkSurfaceFactory = Dali::Integration::Graphics::Vulkan::VkSurfaceFactory;
namespace Vulkan
{

const auto VALIDATION_LAYERS = std::vector< const char* >{

  //"VK_LAYER_LUNARG_screenshot",           // screenshot
  "VK_LAYER_RENDERDOC_Capture",
  "VK_LAYER_LUNARG_parameter_validation", // parameter
  //"VK_LAYER_LUNARG_vktrace",              // vktrace ( requires vktrace connection )
  //"VK_LAYER_LUNARG_monitor",             // monitor
  "VK_LAYER_LUNARG_swapchain",           // swapchain
  "VK_LAYER_GOOGLE_threading",           // threading
  "VK_LAYER_LUNARG_api_dump",            // api
  "VK_LAYER_LUNARG_object_tracker",      // objects
  "VK_LAYER_LUNARG_core_validation",     // core
  "VK_LAYER_GOOGLE_unique_objects",      // unique objects
  "VK_LAYER_GOOGLE_unique_objects",      // unique objects
  "VK_LAYER_LUNARG_standard_validation", // standard
};

Graphics::Graphics() = default;
Graphics::~Graphics() = default;

// Create methods -----------------------------------------------------------------------------------------------
void Graphics::Create()
{

  auto extensions = PrepareDefaultInstanceExtensions();

  auto layers = vk::enumerateInstanceLayerProperties();
  std::vector<const char*> validationLayers;
  for( auto&& reqLayer : VALIDATION_LAYERS )
  {
    for( auto&& prop : layers.value )
    {
      DALI_LOG_STREAM( gVulkanFilter, Debug::General, prop.layerName );
      if( std::string(prop.layerName) == reqLayer )
      {
        validationLayers.push_back(reqLayer);
      }
    }
  }

  CreateInstance(extensions, validationLayers);
  PreparePhysicalDevice();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-larger-than="
void Graphics::CreateDevice()
{
  auto queueInfos = GetQueueCreateInfos();
  {
    auto maxQueueCountPerFamily = 0u;
    for( auto&& info : queueInfos )
    {
      maxQueueCountPerFamily = std::max( info.queueCount, maxQueueCountPerFamily );
    }

    auto priorities = std::vector<float>( maxQueueCountPerFamily );
    std::fill( priorities.begin(), priorities.end(), 1.0f );

    for( auto& info : queueInfos )
    {
      info.setPQueuePriorities( priorities.data() );
    }

    std::vector< const char* > extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    auto info = vk::DeviceCreateInfo{};
    info.setEnabledExtensionCount(U32(extensions.size()))
            .setPpEnabledExtensionNames(extensions.data())
            .setPEnabledFeatures(&(*mPhysicalDeviceFeatures))
            .setPQueueCreateInfos(queueInfos.data())
            .setQueueCreateInfoCount(U32(queueInfos.size()));

    mDevice = VkAssert(mPhysicalDevice.createDevice(info, *mAllocator));
  }

  // create Queue objects
  for(auto& queueInfo : queueInfos)
  {
    for(auto i = 0u; i < queueInfo.queueCount; ++i)
    {
      auto queue = mDevice.getQueue(queueInfo.queueFamilyIndex, i);

      // based on family push queue instance into right array
      auto flags = mQueueFamilyProperties[queueInfo.queueFamilyIndex].queueFlags;
      if(flags & vk::QueueFlagBits::eGraphics)
      {
        mGraphicsQueues.emplace_back(
                MakeUnique<Queue>(*this, queue, queueInfo.queueFamilyIndex, i, flags));
      }
      if(flags & vk::QueueFlagBits::eTransfer)
      {
        mTransferQueues.emplace_back(
                MakeUnique<Queue>(*this, queue, queueInfo.queueFamilyIndex, i, flags));
      }
      if(flags & vk::QueueFlagBits::eCompute)
      {
        mComputeQueues.emplace_back(
                MakeUnique<Queue>(*this, queue, queueInfo.queueFamilyIndex, i, flags));
      }

      // todo: present queue
    }
  }

  mPipelineDatabase = std::make_unique<PipelineCache>( *this );
  mResourceCacheMap[std::this_thread::get_id()] = MakeUnique<ResourceCache>();
}
#pragma GCC diagnostic pop

void Graphics::GetPhysicalDeviceProperties()
{
  // store data on heap to keep object smaller
  mPhysicalDeviceProperties =
    MakeUnique<vk::PhysicalDeviceProperties>(mPhysicalDevice.getProperties());
  mPhysicalDeviceMemoryProperties =
    MakeUnique<vk::PhysicalDeviceMemoryProperties>(mPhysicalDevice.getMemoryProperties());
  mPhysicalDeviceFeatures =
    MakeUnique<vk::PhysicalDeviceFeatures>(mPhysicalDevice.getFeatures());
}

void Graphics::GetQueueFamilyProperties()
{
  mQueueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
}

FBID Graphics::CreateSurface(std::unique_ptr< SurfaceFactory > surfaceFactory)
{
  // create surface from the factory
  auto surfaceRef = Surface::New( *this, std::move(surfaceFactory) );

  if( surfaceRef->Create() )
  {

    // map surface to FBID
    auto fbid = ++mBaseFBID;
    mSurfaceFBIDMap[fbid] = SwapchainSurfacePair{ RefCountedSwapchain{}, surfaceRef };
    return fbid;
  }
  return -1;
}

RefCountedSwapchain Graphics::CreateSwapchainForSurface( RefCountedSurface surface )
{
  auto swapchain = Swapchain::New( *this,
                                   GetGraphicsQueue(0u),
                                   surface, 4, 0 );

  // store swapchain in the correct pair
  for( auto&& val : mSurfaceFBIDMap )
  {
    if( val.second.surface == surface )
    {
      val.second.swapchain = swapchain;
      break;
    }
  }

  return swapchain;
}

RefCountedShader Graphics::CreateShader()
{
  NotImplemented()
}

RefCountedPipeline Graphics::CreatePipeline()
{
  NotImplemented()
}

RefCountedFence Graphics::CreateFence( const vk::FenceCreateInfo& fenceCreateInfo )
{
  auto refCountedFence = Fence::New(*this);

  VkAssert(mDevice.createFence(&fenceCreateInfo, mAllocator.get(), refCountedFence->Ref()));

  return refCountedFence;
}

RefCountedBuffer Graphics::CreateBuffer(size_t size, BufferType type)
{
  auto usageFlags = vk::BufferUsageFlags{};

  switch ( type ) {
    case BufferType::VERTEX: {
      usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
      break;
    };
    case BufferType::INDEX: {
      usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
      break;
    };
    case BufferType::UNIFORM: {
      usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
      break;
    };
    case BufferType::SHADER_STORAGE: {
      usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
      break;
    };
  }

  auto info = vk::BufferCreateInfo{};
  info.setSharingMode(vk::SharingMode::eExclusive);
  info.setSize(size);
  info.setUsage(usageFlags | vk::BufferUsageFlagBits::eTransferDst);

  auto refCountedBuffer = Buffer::New(*this, info);

  VkAssert(mDevice.createBuffer(&info, mAllocator.get(), refCountedBuffer->Ref()));

  AddBuffer(refCountedBuffer);

  return refCountedBuffer;
}

RefCountedBuffer Graphics::CreateBuffer( const vk::BufferCreateInfo& bufferCreateInfo )
{
  auto refCountedBuffer = Buffer::New(*this, bufferCreateInfo);

  VkAssert(mDevice.createBuffer(&bufferCreateInfo, mAllocator.get(), refCountedBuffer->Ref()));

  AddBuffer(refCountedBuffer);

  return refCountedBuffer;
}

RefCountedFramebuffer Graphics::CreateFramebuffer()
{
  NotImplemented()
}

RefCountedImage Graphics::CreateImage()
{
  NotImplemented()
}

RefCountedImageView Graphics::CreateImageView()
{
  NotImplemented()
}

RefCountedDescriptorPool Graphics::CreateDescriptorPool()
{
  NotImplemented()
}

RefCountedCommandPool Graphics::CreateCommandPool(const vk::CommandPoolCreateInfo& info)
{
  return CommandPool::New( *this,
                           vk::CommandPoolCreateInfo{}.setQueueFamilyIndex( 0u )
                                                      .setFlags( vk::CommandPoolCreateFlagBits::eResetCommandBuffer ) );
}

RefCountedCommandBuffer Graphics::CreateCommandBuffer()
{
  NotImplemented()
}

std::vector<RefCountedCommandBuffer> Graphics::CreateCommandBuffers()
{
  NotImplemented()
}

RefCountedGpuMemoryBlock Graphics::CreateGpuMemoryBlock()
{
  NotImplemented()
}

RefCountedDescriptorSet Graphics::CreateDescriptorSet()
{
  NotImplemented()
}

RefCountedSampler Graphics::CreateSampler()
{
  NotImplemented()
}
// --------------------------------------------------------------------------------------------------------------

// Actions ------------------------------------------------------------------------------------------------------
vk::Result Graphics::WaitForFence( RefCountedFence fence, uint32_t timeout )
{
  return mDevice.waitForFences(1, *fence, VK_TRUE, timeout);
}
#pragma GCC diagnostic pop

vk::Result Graphics::WaitForFences( const std::vector<RefCountedFence>& fences, bool waitAll, uint32_t timeout )
{
  std::vector<vk::Fence> vkFenceHandles{};
  std::transform(fences.begin(),
                 fences.end(),
                 std::back_inserter(vkFenceHandles),
                 [](RefCountedFence entry){ return entry->GetVkHandle(); });

    auto priorities = std::vector<float>( maxQueueCountPerFamily );
    std::fill( priorities.begin(), priorities.end(), 1.0f );

  return mDevice.waitForFences(vkFenceHandles, vk::Bool32(waitAll), timeout);
}

vk::Result Graphics::ResetFence( RefCountedFence fence )
{
  return mDevice.resetFences(1, *fence);
}

vk::Result Graphics::ResetFences( const std::vector<RefCountedFence>& fences )
{
  std::vector<vk::Fence> vkFenceHandles{};
  std::transform(fences.begin(),
                 fences.end(),
                 std::back_inserter(vkFenceHandles),
                 [](RefCountedFence entry){ return entry->GetVkHandle(); });

  return mDevice.resetFences(vkFenceHandles);
}
// --------------------------------------------------------------------------------------------------------------

// Getters ------------------------------------------------------------------------------------------------------
RefCountedSurface Graphics::GetSurface( FBID surfaceId )
{
  // TODO: FBID == 0 means default framebuffer, but there should be no
  // such thing as default framebuffer.
  if( surfaceId == 0 )
  {
    return mSurfaceFBIDMap.begin()->second.surface;
  }
  return mSurfaceFBIDMap[surfaceId].surface;
}

RefCountedSwapchain Graphics::GetSwapchainForSurface( RefCountedSurface surface )
{
  for( auto&& val : mSurfaceFBIDMap )
  {
    if( val.second.surface == surface )
    {
      return val.second
              .swapchain;
    }
  }
  return RefCountedSwapchain();
}

RefCountedSwapchain Graphics::GetSwapchainForFBID( FBID surfaceId )
{
  if(surfaceId == 0)
  {
    return mSurfaceFBIDMap.begin()->second.swapchain;
  }
  return mSurfaceFBIDMap[surfaceId].swapchain;
}
#pragma GCC diagnostic pop

vk::Device Graphics::GetDevice() const
{
  return mDevice;
}

vk::PhysicalDevice Graphics::GetPhysicalDevice() const
{
  return mPhysicalDevice;
}

vk::Instance Graphics::GetInstance() const
{
  return mInstance;
}

const vk::AllocationCallbacks& Graphics::GetAllocator() const
{
  return *mAllocator;
}

GpuMemoryManager& Graphics::GetDeviceMemoryManager() const
{
  return *mDeviceMemoryManager;
}

const vk::PhysicalDeviceMemoryProperties& Graphics::GetMemoryProperties() const
{
  return *mPhysicalDeviceMemoryProperties;
}

Queue& Graphics::GetGraphicsQueue(uint32_t index) const
{
  // todo: at the moment each type of queue may use only one, indices greater than 0 are invalid
  // this will change in the future
  assert(index == 0u && "Each type of queue may use only one, indices greater than 0 are invalid!");

  return *mGraphicsQueues[0]; // will be mGraphicsQueues[index]
}

Queue& Graphics::GetTransferQueue(uint32_t index) const
{
  // todo: at the moment each type of queue may use only one, indices greater than 0 are invalid
  // this will change in the future
  assert(index == 0u && "Each type of queue may use only one, indices greater than 0 are invalid!");

  return *mTransferQueues[0]; // will be mGraphicsQueues[index]
}

Queue& Graphics::GetComputeQueue(uint32_t index) const
{
  // todo: at the moment each type of queue may use only one, indices greater than 0 are invalid
  // this will change in the future
  assert(index == 0u && "Each type of queue may use only one, indices greater than 0 are invalid!");

  return *mComputeQueues[0]; // will be mGraphicsQueues[index]
}

Queue& Graphics::GetPresentQueue() const
{
  // fixme: should be a dedicated presentation queue
  return GetGraphicsQueue(0);
}

Platform Graphics::GetDefaultPlatform() const
{
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
  mPlatform = Platform::WAYLAND;
#elif VK_USE_PLATFORM_XCB_KHR
  mPlatform = Platform::XCB;
#elif VK_USE_PLATFORM_XLIB_KHR
  mPlatform =  Platform::XLIB;
#else
  return mPlatform;
#endif
}

Dali::Graphics::API::Controller& Graphics::GetController()
{
  if(!mGfxController)
  {
    mGfxController = Dali::Graphics::VulkanAPI::Controller::New(*this);
  }

  return *mGfxController;
}

PipelineCache& Graphics::GetPipelineCache()
{
  return *mPipelineDatabase;
}
// --------------------------------------------------------------------------------------------------------------

// Cache manipulation methods -----------------------------------------------------------------------------------
void Graphics::AddBuffer( Handle<Buffer> buffer )
{
  GetResourceCache(std::this_thread::get_id())->AddBuffer(std::move(buffer));
}

void Graphics::AddImage( Handle<Image> image )
{
  GetResourceCache(std::this_thread::get_id())->AddImage(std::move(image));
}

void Graphics::AddShader( Handle<Shader> shader )
{
  GetResourceCache(std::this_thread::get_id())->AddShader(std::move(shader));
}

void Graphics::AddCommandPool( Handle<CommandPool> pool )
{
  GetResourceCache(std::this_thread::get_id())->AddCommandPool(std::move(pool));
}

void Graphics::AddDescriptorPool( Handle<DescriptorPool> pool )
{
  GetResourceCache(std::this_thread::get_id())->AddDescriptorPool(std::move(pool));
}

void Graphics::AddFramebuffer( Handle<Framebuffer> framebuffer )
{
  GetResourceCache(std::this_thread::get_id())->AddFramebuffer(std::move(framebuffer));
}

RefCountedShader Graphics::FindShader( vk::ShaderModule shaderModule )
{
  return GetResourceCache(std::this_thread::get_id())->FindShader(shaderModule);
}

RefCountedImage Graphics::FindImage( vk::Image image )
{
  return GetResourceCache(std::this_thread::get_id())->FindImage(image);
}

void Graphics::RemoveBuffer( Buffer& buffer )
{
  GetResourceCache(std::this_thread::get_id())->RemoveBuffer(buffer);
}

void Graphics::RemoveShader( Shader& shader )
{
  GetResourceCache(std::this_thread::get_id())->RemoveShader(shader);
}

void Graphics::RemoveCommandPool( CommandPool& commandPool )
{
  GetResourceCache(std::this_thread::get_id())->RemoveCommandPool(commandPool);
}

void Graphics::RemoveDescriptorPool( DescriptorPool& pool )
{
  GetResourceCache(std::this_thread::get_id())->RemoveDescriptorPool(pool);
}

void Graphics::RemoveFramebuffer( Framebuffer& framebuffer )
{
  GetResourceCache(std::this_thread::get_id())->RemoveFramebuffer(framebuffer);
}

void Graphics::RemoveSampler( Sampler& sampler )
{
  GetResourceCache(std::this_thread::get_id())->RemoveSampler(sampler);
}

void Graphics::CollectGarbage()
{
  GetResourceCache(std::this_thread::get_id())->CollectGarbage();
}

void Graphics::DiscardResource( std::function<void()> deleter )
{
  GetResourceCache(std::this_thread::get_id())->EnqueueDiscardOperation(std::move(deleter));
}
// --------------------------------------------------------------------------------------------------------------


void Graphics::CreateInstance( const std::vector<const char*>& extensions, const std::vector<const char*>& validationLayers )
{
  auto info = vk::InstanceCreateInfo{};

  info.setEnabledExtensionCount(U32(extensions.size()))
      .setPpEnabledExtensionNames(extensions.data())
      .setEnabledLayerCount(U32(validationLayers.size()))
      .setPpEnabledLayerNames(validationLayers.data());

#if defined(DEBUG_ENABLED)
  if( ! getenv("LOG_VULKAN") )
  {
    info.setEnabledLayerCount(0);
  }
#endif

  mInstance = VkAssert(vk::createInstance(info, *mAllocator));
}

void Graphics::DestroyInstance()
{
  if(mInstance)
  {
    mInstance.destroy(*mAllocator);
    mInstance = nullptr;
  }
}


void Graphics::PreparePhysicalDevice()
{
  auto devices = VkAssert(mInstance.enumeratePhysicalDevices());
  assert(!devices.empty() && "No Vulkan supported device found!");

  // if only one, pick first
  mPhysicalDevice = nullptr;
  if(devices.size() == 1)
  {
    mPhysicalDevice = devices[0];
  }
  else // otherwise look for one which is a graphics device
  {
    for(auto& device : devices)
    {
      auto properties =device.getProperties();
      if(properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ||
         properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
      {
        mPhysicalDevice = device;
        break;
      }
    }
  }

  assert(mPhysicalDevice && "No suitable Physical Device found!");

  GetPhysicalDeviceProperties();

  GetQueueFamilyProperties();

  mDeviceMemoryManager = GpuMemoryManager::New( *this );
}

void Graphics::GetPhysicalDeviceProperties()
{
  // store data on heap to keep object smaller
  mPhysicalDeviceProperties =
    MakeUnique<vk::PhysicalDeviceProperties>(mPhysicalDevice.getProperties());
  mPhysicalDeviceMemoryProperties =
    MakeUnique<vk::PhysicalDeviceMemoryProperties>(mPhysicalDevice.getMemoryProperties());
  mPhysicalDeviceFeatures =
    MakeUnique<vk::PhysicalDeviceFeatures>(mPhysicalDevice.getFeatures());
}

void Graphics::GetQueueFamilyProperties()
{
  mQueueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-larger-than="
std::vector< vk::DeviceQueueCreateInfo > Graphics::GetQueueCreateInfos()
{
  // surface is needed in order to find a family that supports presentation to this surface
  // fixme: assuming all surfaces will be compatible with the queue family
  assert(!mSurfaceFBIDMap.empty() &&
         "At least one surface has to be created before creating VkDevice!");

  std::vector< vk::DeviceQueueCreateInfo > queueInfos{};

  constexpr uint8_t MAX_QUEUE_TYPES = 3;
  // find suitable family for each type of queue
  uint32_t familyIndexType[MAX_QUEUE_TYPES];
  std::fill(&familyIndexType[0], &familyIndexType[MAX_QUEUE_TYPES], -1u);

  // Graphics
  auto& graphicsFamily = familyIndexType[0];

  // Transfer
  auto& transferFamily = familyIndexType[1];

  // Transfer
  auto& presentFamily = familyIndexType[2];

  auto queueFamilyIndex = 0u;
  for(auto& prop : mQueueFamilyProperties)
  {
    if((prop.queueFlags & vk::QueueFlagBits::eGraphics) && graphicsFamily == -1u)
    {
      graphicsFamily = queueFamilyIndex;
    }
    if((prop.queueFlags & vk::QueueFlagBits::eTransfer) && transferFamily == -1u)
    {
      transferFamily = queueFamilyIndex;
    }
    if(mPhysicalDevice.getSurfaceSupportKHR(queueFamilyIndex, mSurfaceFBIDMap.begin()->second.surface->GetSurfaceKHR())
           .value &&
       presentFamily == -1u)
    {
      presentFamily = queueFamilyIndex;
    }
    ++queueFamilyIndex;
  }

  assert(graphicsFamily != -1u && "No queue family that supports graphics operations!");
  assert(transferFamily != -1u && "No queue family that supports transfer operations!");
  assert(presentFamily != -1u && "No queue family that supports present operations!");

  // todo: we may require that the family must be same for all types of operations, it makes
  // easier to handle synchronisation related issues.

  // sort queues
  std::sort(&familyIndexType[0], &familyIndexType[MAX_QUEUE_TYPES]);

  // allocate all queues from graphics family
  uint32_t prevQueueFamilyIndex = -1u;

  for(auto i = 0u; i < MAX_QUEUE_TYPES; ++i)
  {
    auto& familyIndex = familyIndexType[i];
    if(prevQueueFamilyIndex == familyIndex)
    {
      continue;
    }

    auto& queueCount = mQueueFamilyProperties[familyIndex].queueCount;

    // fill queue create info for the family.
    // note the priorities are not being set as local pointer will out of scope, this
    // will be fixed by the caller function
    auto info = vk::DeviceQueueCreateInfo{}
                    .setPQueuePriorities(nullptr)
                    .setQueueCount(queueCount)
                    .setQueueFamilyIndex(familyIndex);
    queueInfos.push_back(info);
    prevQueueFamilyIndex = familyIndex;
  }

  return queueInfos;
}
#pragma GCC diagnostic pop

std::vector<const char*> Graphics::PrepareDefaultInstanceExtensions()
{
  auto extensions = vk::enumerateInstanceExtensionProperties();

  std::string extensionName;

  bool xlibAvailable    { false };
  bool xcbAvailable     { false };
  bool waylandAvailable { false };

  for( auto&& ext : extensions.value )
  {
    extensionName = ext.extensionName;
    if( extensionName == VK_KHR_XCB_SURFACE_EXTENSION_NAME )
    {
      xcbAvailable = true;
    }
    else if( extensionName == VK_KHR_XLIB_SURFACE_EXTENSION_NAME )
    {
      xlibAvailable = true;
    }
    else if( extensionName == VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME )
    {
      waylandAvailable = true;
    }
  }

  std::vector<const char*> retval{};

  // depending on the platform validate extensions
  auto platform = GetDefaultPlatform();

  if( platform != Platform::UNDEFINED )
  {
    if (platform == Platform::XCB && xcbAvailable)
    {
      retval.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    }
    else if (platform == Platform::XLIB && xlibAvailable)
    {
      retval.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    }
    else if (platform == Platform::WAYLAND && waylandAvailable)
    {
      retval.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    }
  }
  else // try to determine the platform based on available extensions
  {
    if (xcbAvailable)
    {
      mPlatform = Platform::XCB;
      retval.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    }
    else if (xlibAvailable)
    {
      mPlatform = Platform::XLIB;
      retval.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    }
    else if (waylandAvailable)
    {
      mPlatform = Platform::WAYLAND;
      retval.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    }
    else
    {
      // can't determine the platform!
      mPlatform = Platform::UNDEFINED;
    }
  }

  // other essential extensions
  retval.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
  retval.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );

  return retval;
}

std::unique_ptr<ResourceCache>& Graphics::GetResourceCache(std::thread::id threadId)
{
  if( mResourceCacheMap.count(threadId) == 0 )
  {
    std::lock_guard<std::mutex> lock{ mMutex };
    mResourceCacheMap[threadId] = MakeUnique< ResourceCache >();
  }

  return mResourceCacheMap[threadId];
}

} // namespace Vulkan
} // namespace Graphics
} // namespace Dali