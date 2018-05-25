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

bool ExportMeshs(const char *szPathName, const RawModel &raw, bool bWorldSpace);
bool ExportMaterials(const char *szPathName, const RawModel &raw);

#endif // !__RAW2CROSS_H__
