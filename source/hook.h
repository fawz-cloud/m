#pragma once

#include <jni.h>
#include "config.h"

void install_hooks(const SpoofConfig &config);
void install_jni_hooks(JNIEnv *env, const SpoofConfig &config);
