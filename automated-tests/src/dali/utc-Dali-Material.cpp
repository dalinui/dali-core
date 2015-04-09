/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
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

#include <dali/public-api/dali-core.h>
#include <dali-test-suite-utils.h>

using namespace Dali;

#include <mesh-builder.h>

void material_test_startup(void)
{
  test_return_value = TET_UNDEF;
}

void material_test_cleanup(void)
{
  test_return_value = TET_PASS;
}

int UtcDaliMaterialNew01(void)
{
  TestApplication application;

  Shader shader = Shader::New("vertexSrc", "fragmentSrc");
  Material material = Material::New(shader);

  DALI_TEST_EQUALS( (bool)material, true, TEST_LOCATION );
  END_TEST;
}

int UtcDaliMaterialNew02(void)
{
  TestApplication application;
  Material material;
  DALI_TEST_EQUALS( (bool)material, false, TEST_LOCATION );
  END_TEST;
}

int UtcDaliMaterialDownCast01(void)
{
  TestApplication application;
  Shader shader = Shader::New("vertexSrc", "fragmentSrc");
  Material material = Material::New(shader);

  BaseHandle handle(material);
  Material material2 = Material::DownCast(handle);
  DALI_TEST_EQUALS( (bool)material2, true, TEST_LOCATION );
  END_TEST;
}

int UtcDaliMaterialDownCast02(void)
{
  TestApplication application;

  Handle handle = Handle::New(); // Create a custom object
  Material material = Material::DownCast(handle);
  DALI_TEST_EQUALS( (bool)material, false, TEST_LOCATION );
  END_TEST;
}
