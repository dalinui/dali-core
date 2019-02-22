#ifndef DALI_INTERNAL_SCENE_H
#define DALI_INTERNAL_SCENE_H

/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
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
#include <dali/integration-api/scene.h>
#include <dali/public-api/math/vector2.h>
#include <dali/public-api/actors/layer.h>
#include <dali/public-api/render-tasks/render-task-list.h>
#include <dali/integration-api/render-surface.h>
#include <dali/internal/common/owner-pointer.h>
#include <dali/internal/event/actors/layer-impl.h>
#include <dali/internal/event/render-tasks/render-task-defaults.h>

namespace Dali
{

namespace Internal
{

class Layer;
class LayerList;
class CameraActor;
class RenderTaskList;
class FrameBuffer;

using FrameBufferPtr = IntrusivePtr<FrameBuffer>;
using ScenePtr = IntrusivePtr<Scene>;

/**
 * @brief Scene creates a "world" that can be bound to a surface for rendering.
 */
class Scene : public BaseObject, public RenderTaskDefaults
{

public:

  /**
   * @copydoc Dali::Integration::Scene::New
   */
  static ScenePtr New( Size size );

  /**
   * virtual destructor
   */
  virtual ~Scene();

  /**
   * @copydoc Dali::Integration::Scene::Add
   */
  void Add(Actor& actor);

  /**
   * @copydoc Dali::Integration::Scene::Remove
   */
  void Remove(Actor& actor);

  /**
   * @copydoc Dali::Integration::Scene::GetSize
   */
  Size GetSize() const;

  /**
   * @copydoc Dali::Integration::Scene::SetDpi
   */
  void SetDpi( Vector2 dpi );

  /**
   * @copydoc Dali::Integration::Scene::GetDpi
   */
  Vector2 GetDpi() const;

  /**
   * @copydoc Dali::Integration::Scene::GetRenderTaskList
   */
  RenderTaskList& GetRenderTaskList() const;

  /**
   * @copydoc Dali::Integration::Scene::GetRootLayer
   */
  Dali::Layer GetRootLayer() const;

  /**
   * @copydoc Dali::Integration::Scene::GetLayerCount
   */
  uint32_t GetLayerCount() const;

  /**
   * @copydoc Dali::Integration::Scene::GetLayer
   */
  Dali::Layer GetLayer(uint32_t depth) const;

  /**
   * @copydoc Dali::Integration::Scene::SetSurface
   */
  void SetSurface( Integration::RenderSurface& surface );

  /**
   * Retrieve the render surface the scene is binded to.
   * @return The render surface.
   */
  Integration::RenderSurface* GetSurface() const;

  /**
   * Retrieve the ordered list of on-stage layers.
   * @return The layer-list.
   */
  LayerList& GetLayerList() const;

  /**
   * Request that the depth tree is rebuilt
   */
  void RequestRebuildDepthTree();

  /**
   * Rebuilds the depth tree at the end of the event frame if
   * it was requested this frame.
   */
  void RebuildDepthTree();

  /**
   * @brief Sets the background color of the render　surface.
   * @param[in] color The new background color
   */
  void SetBackgroundColor(Vector4 color);

  /**
   * @brief Gets the background color of the render　surface.
   * @return The background color
   */
  Vector4 GetBackgroundColor() const;

public:

  /**
   * From RenderTaskDefaults; retrieve the default root actor.
   * @return The default root actor.
   */
  virtual Actor& GetDefaultRootActor();

  /**
   * From RenderTaskDefaults; retrieve the default camera actor.
   * @return The default camera actor.
   */
  virtual CameraActor& GetDefaultCameraActor();

private:

  // Constructor
  Scene( Size size );

  /**
   * Second-phase constructor.
   */
  void Initialize();

  // Undefined
  Scene(const Scene&) = delete;

  // Undefined
  Scene& operator=(const Scene& rhs) = delete;

private:

  Integration::RenderSurface* mSurface;

  // The scene-size may be different with the surface-size
  Size mSize;
  Size mSurfaceSize;

  Vector2 mDpi;

  LayerPtr mRootLayer;

  // Ordered list of currently on-stage layers
  OwnerPointer<LayerList> mLayerList;

  IntrusivePtr<CameraActor> mDefaultCamera;

  // The list of render-tasks
  IntrusivePtr<RenderTaskList> mRenderTaskList;

  // The frame buffer
  FrameBufferPtr mFrameBuffer;

  bool mDepthTreeDirty:1;  ///< True if the depth tree needs recalculating
};

} // Internal

// Get impl of handle
inline Internal::Scene& GetImplementation(Dali::Integration::Scene& scene)
{
  DALI_ASSERT_ALWAYS( scene && "Scene handle is empty" );
  Dali::RefObject& object = scene.GetBaseObject();
  return static_cast<Internal::Scene&>(object);
}

inline const Internal::Scene& GetImplementation(const Dali::Integration::Scene& scene)
{
  DALI_ASSERT_ALWAYS( scene && "Scene handle is empty" );
  const Dali::RefObject& object = scene.GetBaseObject();
  return static_cast<const Internal::Scene&>(object);
}

} // Dali

#endif // DALI_INTERNAL_SCENE_H
