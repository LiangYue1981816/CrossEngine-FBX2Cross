/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#ifndef __RAW2CROSS_H__
#define __RAW2CROSS_H__

#include <memory>
#include <string>
#include "RawModel.h"

void splitfilename(const char *name, char *fname, char *ext);

bool ExportMesh(const char *szFileName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels);
bool ExportMaterial(const char *szPathName, const RawModel &rawModel);
bool ExportMeshXML(const char *szFileName, const char *szMeshFileName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels);

#endif // !__RAW2CROSS_H__
